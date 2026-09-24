#ifndef CGI_PROCESS_HPP
# define CGI_PROCESS_HPP

#include "Router.hpp"
#include <string>

enum CgiReadStatus
{
    CGI_READING,
    CGI_OUTPUT_COMPLETE,
    CGI_READ_ERROR
};

class CgiProcess
{
    public:
        CgiProcess();
        ~CgiProcess();
        void startCgi(const CgiInfo &cgiInfo, const Request &request, Response &response);
        void finishCgi(Response &response);
        CgiReadStatus readFromPipe();
        bool checkChild();

    private:
        pid_t       pid_;
        int         cgiOutputFd_;
        std::string buffer_;
        bool        childExitCollected_;
        bool        childSucceeded_;
        CgiProcess(const CgiProcess& other);
        CgiProcess& operator=(const CgiProcess& other);};

#endif