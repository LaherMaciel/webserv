#ifndef CONNECTION_HPP
# define CONNECTION_HPP

#include <iostream>
#include <string>
#include "RequestParser.hpp"
#include "Request.hpp"
#include "CgiProcess.hpp"

class Response;

enum ConnectionStatus
{
    CLOSE_CONNECTION,
    WAIT_FOR_MORE,
    REQUEST_READY,
    CGI_STARTED,
    CGI_WAITING_FOR_EXIT,
    CGI_IO_ERROR,
    RESPONSE_READY,
    CONTINUE_CONNECTION,
    RESPONSE_SENT
};

class Connection
{
    public:
        Connection(int fd, const std::string &remoteAddress);
        ~Connection();
        ConnectionStatus handleRequest();
        int             readFromSocket();
        void            queueResponse(Response &response);
        ConnectionStatus sendResponse();
        ConnectionStatus queueErrorResponse(int code, std::string version = "HTTP/1.1", std::string body = "", std::string contentType = "text/plain");
        int getFd() const;
        int getCgiOutputFd() const;
        int getCgiInputFd() const;
        const Request&  getRequest() const;
        bool            getCloseAfterResponse() const;
        void startCgi(const CgiInfo &cgiInfo);
        ConnectionStatus readFromCGIPipe();
        ConnectionStatus checkCgiChild();
        bool cgiCompletionIsPending() const;
        bool cgiAbortIsPending() const;
        bool cgiHasTimedOut() const;
        bool cgiIsRunning() const;
        CgiCleanupStatus abortCgi();
        void closeClientFd();
        void resetCgiProcess();
        void            resetRequest();
        void            resetForNewRequest();
        bool            hasPendingResponse() const;
        bool           hasBufferedInput() const;
        ConnectionStatus writeToCGIPipe();
        void applyConnectionClosePolicy();

    private:
        int             fd_;
        std::string	    in_buffer_;
        std::string     out_buffer_;
        size_t          bytes_sent_;
        CgiProcess      cgiProcess_;
        Connection(const Connection& other);
        Connection& operator=(const Connection& other);
        RequestParser   parser_;
        Request         request_;
        bool            closeAfterResponse_;
        std::string     remoteAddress_;//ip of client
};

#endif
