#include "CgiProcess.hpp"
#include "webserv.hpp"
#include "Response.hpp"
#include "Request.hpp"
#include <unistd.h>//for pipe(), fork(), dup2()
#include <sys/wait.h>//for waitpid()
#include <signal.h>//for kill()
#include <cerrno>//for EINTR

CgiProcess::CgiProcess()
    : pid_(-1), cgiOutputFd_(-1), buffer_(""),
      childStatus_(CHILD_NOT_STARTED), outputEof_(false), killSent_(false) {}

CgiProcess::~CgiProcess() { closeOutputFd(); }

int CgiProcess::getOutputFd() const { return cgiOutputFd_; }

bool CgiProcess::isWaitingForExit() const
{
    return outputEof_ && childStatus_ == CHILD_RUNNING && !killSent_;
}

bool CgiProcess::isAbortPending() const
{
    return killSent_ && childStatus_ == CHILD_RUNNING;
}

void CgiProcess::closeOutputFd()
{
    if (cgiOutputFd_ != -1)
    {
        close(cgiOutputFd_);
        cgiOutputFd_ = -1;
    }
}

void CgiProcess::reset()
{
    closeOutputFd();
    pid_ = -1;
    buffer_.clear();
    childStatus_ = CHILD_NOT_STARTED;
    outputEof_ = false;
    killSent_ = false;
}

CgiCleanupStatus CgiProcess::abort()
{
    closeOutputFd();
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
        closeOutputFd();
        return CGI_OUTPUT_COMPLETE;
    }
    closeOutputFd();
    return CGI_READ_ERROR;
}

void CgiProcess::finishCgi(Response &response)
{
    if (childStatus_ != CHILD_SUCCEEDED)
    {
        response.setBody("Internal Server Error: CGI script failed", "text/plain");
        response.setStatusCode(500);
        return;
    }
    size_t headerEnd = buffer_.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
    {
        response.setStatusCode(500);
        response.setBody("Malformed CGI output", "text/plain");
        return;
    }
    std::string cgiHeaders = buffer_.substr(0, headerEnd);
    std::string cgiBody = buffer_.substr(headerEnd + 4);

    response.setStatusCode(200);
    response.setBody(cgiBody, "text/plain");
}

void CgiProcess::startCgi(const CgiInfo &cgiInfo, const Request &request)
{
    int pipefd[2];
    if (pipe(pipefd) == -1)
    {
        throw std::runtime_error("Failed to create pipe");
    }
    pid_ = fork();
    if (pid_ < 0)
    {
        close(pipefd[0]);
        close(pipefd[1]);
        throw std::runtime_error("Failed to fork process");
    }
    if (pid_ == 0) // Child process
    {
        close(pipefd[0]); // Close read end in child
        dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe
        close(pipefd[1]); // Close write end after duplicating
        chdir(cgiInfo.workingDirectory_.c_str()); // Change working directory to script's directory
        //Testing hardcoded script
        std::string scriptFile = "hello.py";
        char *argv[] = {
        const_cast<char *>(cgiInfo.handler_.c_str()),
        const_cast<char *>(scriptFile.c_str()),
        NULL
        };
        std::string methodEnv = "REQUEST_METHOD=" + request.getMethod();
        std::string queryEnv = "QUERY_STRING=" + request.getQuery();
        std::string protocolEnv = "SERVER_PROTOCOL=" + request.getVersion();
        std::string gatewayEnv = "GATEWAY_INTERFACE=CGI/1.1";
        char *envp[] = {
        const_cast<char *>(methodEnv.c_str()),
        const_cast<char *>(queryEnv.c_str()),
        const_cast<char *>(protocolEnv.c_str()),
        const_cast<char *>(gatewayEnv.c_str()),
        NULL
        };
        //Execute the CGI script
        execve(cgiInfo.handler_.c_str(), argv, envp);
        _exit(1);// If execve fails
    }
    else // Parent process
    {
        close(pipefd[1]); // Close write end in parent
        cgiOutputFd_ = pipefd[0];
        childStatus_ = CHILD_RUNNING;
        outputEof_ = false;
        killSent_ = false;
        //apply non-block:
        if (set_non_blocking(cgiOutputFd_) < 0)
        {
            closeOutputFd();
            throw std::runtime_error("Failed to set non-blocking mode for CGI output pipe");
        }
    }
}
