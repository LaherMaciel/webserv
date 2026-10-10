#ifndef CGI_PROCESS_HPP
# define CGI_PROCESS_HPP

#include "Router.hpp"
#include <string>
#include <vector>
#include <set>
#include <map>
#include <unistd.h>//for pipe(), fork(), dup2()

enum CgiReadStatus
{
    CGI_READING,
    CGI_OUTPUT_COMPLETE,
    CGI_READ_ERROR
};

enum CgiWriteStatus
{
    CGI_WRITING,
    CGI_INPUT_COMPLETE,
    CGI_WRITE_ERROR
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
        bool completionIsPending() const;
        bool abortIsPending() const;
        CgiCleanupStatus abort();
        int getOutputFd() const;
        int getInputFd() const;
        void reset();
        std::vector<std::string> buildEnvp(const Request &request, const CgiInfo &cgiInfo);
        CgiWriteStatus writeToPipe(const std::string &data);

    private:
        pid_t       pid_;
        int         cgiOutputFd_;
        int         cgiInputFd_;
        size_t      inputOffset_;
        std::string buffer_;
        ChildStatus childStatus_;
        bool        outputEof_;
        bool        killSent_;

        void closeFd(int &fd);
        bool processHeader(Response &response, const std::string &headerLine, std::map<std::string,
                            std::string> &parsedHeaders, std::set<std::string> &seenHeaders);
        bool setStatus(Response &response, const std::string &value);

        CgiProcess(const CgiProcess& other);
        CgiProcess& operator=(const CgiProcess& other);
};

#endif
