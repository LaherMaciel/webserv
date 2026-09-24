#ifndef CGI_PROCESS_HPP
# define CGI_PROCESS_HPP

#include "Router.hpp"
#include <string>
class CgiProcess
{
    public:
        CgiProcess();
        ~CgiProcess();
        void startCgi(const CgiInfo &cgiInfo, const Request &request, Response &response);

    private:
        CgiProcess(const CgiProcess& other);
        CgiProcess& operator=(const CgiProcess& other);};

#endif