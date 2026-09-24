#include "CgiProcess.hpp"
#include "Response.hpp"
#include "Request.hpp"
#include <unistd.h>//for pipe(), fork(), dup2()
#include <sys/wait.h>//for waitpid()

CgiProcess::CgiProcess() {}

CgiProcess::~CgiProcess() {}

void CgiProcess::startCgi(const CgiInfo &cgiInfo, const Request &request, Response &response)
{
    (void)cgiInfo; // Suppress unused parameter warning
    (void)request; // Suppress unused parameter warning
    //Create pipes for communication between parent and child
    int pipefd[2];
    if (pipe(pipefd) == -1)
    {
        response.setBody("Internal Server Error: Failed to create pipe", "text/plain");
        response.setStatusCode(500);
        return;
    }
    if (fork() == 0) // Child process
    {
        close(pipefd[0]); // Close read end in child
        dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe
        close(pipefd[1]); // Close write end after duplicating
        chdir(cgiInfo.workingDirectory_.c_str()); // Change working directory to script's directory
        std::string scriptFile = "hello.py";
        // Set the environment variables
            char *argv[] = {
            const_cast<char *>(cgiInfo.handler_.c_str()),
            const_cast<char *>(scriptFile.c_str()),
            NULL
        };

        std::string methodEnv =
            "REQUEST_METHOD=" + request.getMethod();

        std::string queryEnv =
            "QUERY_STRING=" + request.getQuery();

        std::string protocolEnv =
            "SERVER_PROTOCOL=" + request.getVersion();

        std::string gatewayEnv =
            "GATEWAY_INTERFACE=CGI/1.1";

        char *envp[] = {
        const_cast<char *>(methodEnv.c_str()),
        const_cast<char *>(queryEnv.c_str()),
        const_cast<char *>(protocolEnv.c_str()),
        const_cast<char *>(gatewayEnv.c_str()),
        NULL
        };
        // Execute the CGI script
        execve(cgiInfo.handler_.c_str(), argv, envp);
        // If execve fails
        _exit(1);
    }
    else // Parent process
    {
        close(pipefd[1]); // Close write end in parent
        waitpid(-1, NULL, 0); // Wait for child to finish

        char buffer[1024];
        ssize_t bytesRead = read(pipefd[0], buffer, sizeof(buffer) - 1);
        if (bytesRead > 0)
        {
            buffer[bytesRead] = '\0';
            std::string output(buffer);
            size_t headerEnd = output.find("\r\n\r\n");
            if (headerEnd == std::string::npos)
            {
                response.setStatusCode(500);
                response.setBody("Malformed CGI output", "text/plain");
                close(pipefd[0]);
                return;
            }
            std::string cgiHeaders = output.substr(0, headerEnd);
            std::string cgiBody = output.substr(headerEnd + 4);

            response.setStatusCode(200);
            response.setBody(cgiBody, "text/plain");
        }
        else
        {
            response.setBody("Internal Server Error: Failed to read CGI output", "text/plain");
            response.setStatusCode(500);
        }
        close(pipefd[0]); // Close read end after reading
    }
}