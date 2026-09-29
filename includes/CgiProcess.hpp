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

enum ChildStatus
{
    CHILD_NOT_STARTED,
    CHILD_RUNNING,
    CHILD_SUCCEEDED,
    CHILD_FAILED
};

enum CgiCleanupStatus
{
    CGI_CLEANUP_PENDING,
    CGI_CLEANUP_DONE
};

class CgiProcess
{
    public:
        CgiProcess();
        ~CgiProcess();
        void startCgi(const CgiInfo &cgiInfo, const Request &request);
        void finishCgi(Response &response);
        CgiReadStatus readFromPipe();
        bool checkChild();
        bool isWaitingForExit() const;
        bool isAbortPending() const;
        CgiCleanupStatus abort();
        int getOutputFd() const;
        void reset();

    private:
        pid_t       pid_;
        int         cgiOutputFd_;
        std::string buffer_;
        ChildStatus childStatus_;
        bool        outputEof_;
        bool        killSent_;

        void closeOutputFd();

        CgiProcess(const CgiProcess& other);
        CgiProcess& operator=(const CgiProcess& other);
};

#endif
