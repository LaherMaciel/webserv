#include "CgiProcess.hpp"
#include "webserv.hpp"
#include "Response.hpp"
#include "Request.hpp"
#include <unistd.h>//for pipe(), fork(), dup2()
#include <sys/wait.h>//for waitpid()

CgiProcess::CgiProcess() : pid_(-1), cgiOutputFd_(-1), buffer_(""), childExitCollected_(false), childSucceeded_(false) {}

CgiProcess::~CgiProcess() {}

bool CgiProcess::checkChild()
{
    if (childExitCollected_)
        return true;
    int status;
    pid_t result = waitpid(pid_, &status, 0);//blocking
    //pid_t result = waitpid(pid_, &status, WNOHANG); //non-blocking
    if (result == 0)
        return false; // child is still running
    if (result == pid_)
    {
        childExitCollected_ = true;
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
            childSucceeded_ = true;
        return true;
    }
    childExitCollected_ = true;
    childSucceeded_ = false;
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
        return CGI_OUTPUT_COMPLETE;
    }
    return CGI_READ_ERROR;
}

void CgiProcess::finishCgi(Response &response)
{
    while (true)
    {
        CgiReadStatus status = readFromPipe();
        if (status == CGI_READING)
            continue;
        else if (status == CGI_OUTPUT_COMPLETE)
            break;
        else
        {
            response.setBody("Internal Server Error: Failed to read CGI output", "text/plain");
            response.setStatusCode(500);
            close(cgiOutputFd_); // Close read end after reading
            return;
        }
    }
    if (!checkChild())
    {
        close(cgiOutputFd_);
        response.setBody("Internal Server Error: CGI script did not finish", "text/plain");
        response.setStatusCode(500);
        return;
    }
    if (!childSucceeded_)
    {
        response.setBody("Internal Server Error: CGI script failed", "text/plain");
        response.setStatusCode(500);
        close(cgiOutputFd_); // Close read end after reading
        return;
    }
    size_t headerEnd = buffer_.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
    {
        response.setStatusCode(500);
        response.setBody("Malformed CGI output", "text/plain");
        close(cgiOutputFd_); // Close read end after reading
        return;
    }
    std::string cgiHeaders = buffer_.substr(0, headerEnd);
    std::string cgiBody = buffer_.substr(headerEnd + 4);

    response.setStatusCode(200);
    response.setBody(cgiBody, "text/plain");
    close(cgiOutputFd_); // Close read end after reading
}

void CgiProcess::startCgi(const CgiInfo &cgiInfo, const Request &request, Response &response)
{
    int pipefd[2];
    if (pipe(pipefd) == -1)
    {
        response.setBody("Internal Server Error: Failed to create pipe", "text/plain");
        response.setStatusCode(500);
        return;
    }
    pid_ = fork();
    if (pid_ < 0)
    {
        close(pipefd[0]);
        close(pipefd[1]);
        response.setBody("Internal Server Error: Failed to fork process", "text/plain");
        response.setStatusCode(500);
        return;
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
    }
}