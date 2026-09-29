#ifndef CONNECTION_HPP
# define CONNECTION_HPP

#include <iostream>
#include <string>
#include "RequestParser.hpp"
#include "Request.hpp"
#include "CgiProcess.hpp"

#define MAX_HEADER_SIZE 1000

class Response;

enum ConnectionStatus
{
    CLOSE_CONNECTION,
    WAIT_FOR_MORE,
    REQUEST_READY,
    CGI_STARTED,
    CGI_WAITING_FOR_EXIT,
    CGI_IO_ERROR,
    RESPONSE_READY
};

class Connection
{
	public:
        Connection();
        Connection(int fd);
        ~Connection();
        ConnectionStatus handleRequest();
        int readFromSocket();
        void queueResponse(const Response &response);
        ConnectionStatus sendResponse();
        ConnectionStatus queueErrorResponse(int code, std::string version = "HTTP/1.1", std::string body = "", std::string contentType = "text/plain");
        int getFd() const;
        int getCgiOutputFd() const;
        const Request& getRequest() const;
        void startCgi(const CgiInfo &cgiInfo);
        ConnectionStatus readFromCGIPipe();
        ConnectionStatus checkCgiChild();
        bool isWaitingForCgiExit() const;
        bool isCgiAbortPending() const;
        CgiCleanupStatus abortCgi();
        void closeClientFd();
        void resetCgiProcess();

    private:
        int			fd_;
        std::string	in_buffer_;
        std::string out_buffer_;
        size_t      bytes_sent_;
        CgiProcess  cgiProcess_;
        Connection(const Connection& other);
        Connection& operator=(const Connection& other);
        RequestParser	parser_;
        Request         request_;
};

#endif
