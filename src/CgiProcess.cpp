#include "CgiProcess.hpp"
#include "webserv.hpp"
#include "Response.hpp"
#include "Request.hpp"
#include <unistd.h>//for pipe(), fork(), dup2()
#include <sys/wait.h>//for waitpid()
#include <signal.h>//for kill()
#include <cerrno>//for EINTR
#include <vector>
#include <map>
#include <set>
#include <algorithm>//for std::transform and std::replace

/*
curl -i 'http://127.0.0.1:8080/cgi-bin/test_headers.py'
curl -i 'http://127.0.0.1:8080/cgi-bin/test_headers.py?mode=lf'
curl -i 'http://127.0.0.1:8080/cgi-bin/test_headers.py?mode=duplicate'
curl -i 'http://127.0.0.1:8080/cgi-bin/test_headers.py?mode=malformed'
CRLF is \r\n
LF is \n
*/

CgiProcess::CgiProcess()
    : pid_(-1), cgiOutputFd_(-1), cgiInputFd_(-1), inputOffset_(0),
      childStatus_(CHILD_NOT_STARTED), outputEof_(false), killSent_(false) {}

CgiProcess::~CgiProcess() { closeFd(cgiOutputFd_); closeFd(cgiInputFd_); }

int CgiProcess::getOutputFd() const { return cgiOutputFd_; }

int CgiProcess::getInputFd() const { return cgiInputFd_; }

bool CgiProcess::completionIsPending() const
{
    return outputEof_ && childStatus_ == CHILD_RUNNING && !killSent_;
}

bool CgiProcess::abortIsPending() const
{
    return killSent_ && childStatus_ == CHILD_RUNNING;
}

void CgiProcess::closeFd(int &fd)
{
    if (fd != -1)
    {
        close(fd);
        fd = -1;
    }
}

void CgiProcess::reset()
{
    closeFd(cgiOutputFd_);
    closeFd(cgiInputFd_);
    pid_ = -1;
    inputOffset_ = 0;
    buffer_.clear();
    childStatus_ = CHILD_NOT_STARTED;
    outputEof_ = false;
    killSent_ = false;
}

CgiCleanupStatus CgiProcess::abort()
{
    closeFd(cgiOutputFd_);
    closeFd(cgiInputFd_);
    if (childStatus_ == CHILD_RUNNING)
    {
        if (!killSent_)
        {
            kill(pid_, SIGKILL);
            killSent_ = true;
        }
        if (!checkChild())
            return CGI_CLEANUP_PENDING;
    }
    reset();
    return CGI_CLEANUP_DONE;
}

bool CgiProcess::checkChild()
{
    if (childStatus_ == CHILD_SUCCEEDED || childStatus_ == CHILD_FAILED)
        return true;
    if (childStatus_ == CHILD_NOT_STARTED)
        return false;
    int status;
    pid_t result = waitpid(pid_, &status, WNOHANG); //non-blocking
    if (result == 0)
        return false; // child is still running
    if (result == -1 && errno == EINTR)
        return false;
    if (result == pid_)
    {
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
            childStatus_ = CHILD_SUCCEEDED;
        else
            childStatus_ = CHILD_FAILED;
        return true;
    }
    childStatus_ = CHILD_FAILED;
    return true;
}

CgiReadStatus CgiProcess::readFromPipe()
{
    char buffer[IO_CHUNK_SIZE];
    ssize_t bytesRead = read(cgiOutputFd_, buffer, sizeof(buffer));
    if (bytesRead > 0)
    {
        buffer_.append(buffer, bytesRead);
        return CGI_READING;
    }
    if (bytesRead == 0)
    {
        outputEof_ = true;
        closeFd(cgiOutputFd_);
        return CGI_OUTPUT_COMPLETE;
    }
    closeFd(cgiOutputFd_);
    return CGI_READ_ERROR;
}

CgiWriteStatus CgiProcess::writeToPipe(const std::string &data)
{
    if (cgiInputFd_ == -1)
        return CGI_WRITE_ERROR;
    ssize_t bytesWritten = write(cgiInputFd_, data.c_str() + inputOffset_, data.size() - inputOffset_);
    if (bytesWritten >= 0)
    {
        inputOffset_ += bytesWritten;
        if (inputOffset_ >= data.size())
        {
            closeFd(cgiInputFd_);
            return CGI_INPUT_COMPLETE;
        }
        return CGI_WRITING;
    }
    closeFd(cgiInputFd_);
    return CGI_WRITE_ERROR;
}

bool CgiProcess::setStatus(Response &response, const std::string &value)
{
    size_t spacePos = value.find(' ');
    std::string statusCodeStr;
    std::string reasonPhrase;
    if (spacePos == std::string::npos)
        statusCodeStr = value;
    else
    {
        statusCodeStr = value.substr(0, spacePos);
        reasonPhrase = value.substr(spacePos + 1);
    }
    if (statusCodeStr.size() != 3)
        return false;
    int statusCode = std::atoi(statusCodeStr.c_str());
    if (statusCode < 100 || statusCode > 599)
        return false;
    if (reasonPhrase.empty())
        reasonPhrase = httpReasonPhrase(statusCode);
    response.setStatusCode(statusCode, reasonPhrase);
    return true;
}

bool CgiProcess::processHeader(Response &response, const std::string &headerLine, std::map<std::string, std::string> &parsedHeaders, std::set<std::string> &seenHeaders)
{
    size_t colonPos = headerLine.find(':');
    if (colonPos == std::string::npos)
        return false;
    std::string key = headerLine.substr(0, colonPos);
    std::string value = headerLine.substr(colonPos + 1);
    while (!value.empty() && (value[0] == ' ' || value[0] == '\t'))
        value.erase(0, 1);
    if (key.empty() || value.empty())
        return false;
    std::string lowerKey = toLower(key);
    if (!seenHeaders.insert(lowerKey).second)
        return false;
    if (lowerKey == "status")
        return setStatus(response, value);
    else if (lowerKey == "content-type")
    {
        parsedHeaders["Content-Type"] = value;
        return true;
    }
    else if (lowerKey == "content-length" || lowerKey == "connection" || lowerKey == "transfer-encoding")
        return true;
    parsedHeaders[key] = value;
    return true;
}

void CgiProcess::finishCgi(Response &response)
{
    if (childStatus_ != CHILD_SUCCEEDED)
    {
        response.setBody("Internal Server Error: CGI script failed", "text/plain");
        return;
    }
    size_t headerEnd = buffer_.find("\r\n\r\n");
    size_t separator = 4;
    if (headerEnd == std::string::npos)
    {
        headerEnd = buffer_.find("\n\n");
        separator = 2;
        if (headerEnd == std::string::npos)
        {
            response.setBody("Malformed CGI output", "text/plain");
            return;
        }
    }
    std::string cgiHeaders = buffer_.substr(0, headerEnd);
    std::string cgiBody = buffer_.substr(headerEnd + separator);
    size_t lineStart = 0;
    response.setStatusCode(200);
    std::map<std::string, std::string> parsedHeaders;
    std::set<std::string> seenHeaders;
    while (lineStart < cgiHeaders.size())
    {
        size_t lineEnd = cgiHeaders.find("\n", lineStart);
        if (lineEnd == std::string::npos)
            lineEnd = cgiHeaders.size();
        size_t lineLength = lineEnd - lineStart;
        if (lineLength > 0 && cgiHeaders[lineEnd - 1] == '\r')
            lineLength--;
        std::string line = cgiHeaders.substr(lineStart, lineLength);
        if (!processHeader(response, line, parsedHeaders, seenHeaders))
        {
            response.setStatusCode(500);
            response.setBody("Malformed CGI output", "text/plain");
            return;
        }
        lineStart = lineEnd + 1;
    }
    response.setHeaders(parsedHeaders);
    response.setBody(cgiBody);
}

std::vector<std::string> CgiProcess::buildEnvp(const Request &request, const CgiInfo &cgiInfo)
{
    std::vector<std::string> envp;
    envp.push_back("REQUEST_METHOD=" + request.getMethod());
    envp.push_back("QUERY_STRING=" + request.getQuery());
    envp.push_back("SERVER_PROTOCOL=" + request.getVersion());
    envp.push_back("GATEWAY_INTERFACE=CGI/1.1");
    envp.push_back("SCRIPT_NAME=" + cgiInfo.scriptUrlPath_);
    envp.push_back("SCRIPT_FILENAME=" + cgiInfo.scriptFilename_);
    envp.push_back("PATH_INFO=" + cgiInfo.pathInfo_);
    envp.push_back("CONTENT_LENGTH=" + toString(request.getBody().size()));
    for (std::map<std::string, std::string>::const_iterator it = request.getHeaders().begin();
         it != request.getHeaders().end(); ++it)
    {
        std::string headerName = it->first;
        std::replace(headerName.begin(), headerName.end(), '-', '_');
        std::transform(headerName.begin(), headerName.end(), headerName.begin(), ::toupper);
        if (headerName == "CONTENT_TYPE")
            envp.push_back(headerName + "=" + it->second);
        else if (headerName == "CONTENT_LENGTH" || headerName == "TRANSFER_ENCODING")
            continue ;
        else
            envp.push_back("HTTP_" + headerName + "=" + it->second);
    }
    return envp;
}

void CgiProcess::startCgi(const CgiInfo &cgiInfo, const Request &request)
{
    int outputPipe[2];
    if (pipe(outputPipe) == -1)
        throw std::runtime_error("Failed to create output pipe");
    int inputPipe[2];
    if (pipe(inputPipe) == -1)
    {
        close(outputPipe[0]);
        close(outputPipe[1]);
        throw std::runtime_error("Failed to create output pipe");
    }
    pid_ = fork();
    if (pid_ < 0)
    {
        close(outputPipe[0]);
        close(outputPipe[1]);
        close(inputPipe[0]);
        close(inputPipe[1]);
        throw std::runtime_error("Failed to fork process");
    }
    if (pid_ == 0) // Child process
    {
        close(outputPipe[0]); // Close read end in child
        dup2(outputPipe[1], STDOUT_FILENO); // Redirect stdin to pipe
        close(outputPipe[1]); // Close write end after duplicating

        close(inputPipe[1]); // Close write end in child
        dup2(inputPipe[0], STDIN_FILENO);
        close(inputPipe[0]); // Close read end after duplicating
        if (chdir(cgiInfo.workingDirectory_.c_str()) == -1)
            _exit(1);
        char *argv[] = {const_cast<char *>(cgiInfo.interpreterPath_.c_str()),
                        const_cast<char *>(cgiInfo.scriptFilename_.c_str()), NULL};
        std::vector<std::string> envp = buildEnvp(request, cgiInfo);
        std::vector<char *> envpArray;
        for (size_t i = 0; i < envp.size(); ++i)
            envpArray.push_back(const_cast<char *>(envp[i].c_str()));
        envpArray.push_back(NULL);
        //Execute the CGI script
        execve(cgiInfo.interpreterPath_.c_str(), argv, &envpArray[0]);
        _exit(1);// If execve fails
    }
    else // Parent process
    {
        close(inputPipe[0]); // Close read end in parent
        if (request.getBody().empty())
            close(inputPipe[1]); // Close write end in parent if no body to send
        else
            cgiInputFd_ = inputPipe[1];
        close(outputPipe[1]); // Close read end in parent
        cgiOutputFd_ = outputPipe[0];
        childStatus_ = CHILD_RUNNING;
        outputEof_ = false;
        killSent_ = false;
        //apply non-block:
        if (set_non_blocking(cgiOutputFd_) < 0 || (cgiInputFd_ != -1 && set_non_blocking(cgiInputFd_) < 0))
        {
            closeFd(cgiOutputFd_);
            closeFd(cgiInputFd_);
            throw std::runtime_error("Failed to set non-blocking mode for CGI output pipe");
        }
    }
}
