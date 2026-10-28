#ifndef CONNECTION_HPP
# define CONNECTION_HPP

#include <iostream>
#include <string>
#include <ctime>
#include "RequestParser.hpp"
#include "Request.hpp"
#include "CgiProcess.hpp"

class Response;
struct Listener;

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

enum ConnectionState
{
    CONN_NEW,
    CONN_WAITING_FOR_REQUEST,
    CONN_READING_HEADERS,
    CONN_READING_BODY,
    CONN_READY_TO_ROUTE,
    CONN_RUNNING_CGI,
    CONN_WRITING_RESPONSE,
    CONN_CLOSING
};

class Connection
{
    public:
        Connection(int fd, const std::string &remoteAddress, Listener *listener, ServerConfig *defaultConfig);
        ~Connection();
        ConnectionStatus handleRequest();
        int             readFromSocket();
        void            queueResponse(Response &response);
        ConnectionStatus sendResponse();
        ConnectionStatus queueErrorResponse(int code, std::string version = "HTTP/1.1", std::string body = "", std::string contentType = "text/plain");
        int             getFd() const;
        Listener *getListener() const;
        void            setSelectedConfig(ServerConfig *config);
        ServerConfig *getSelectedConfig() const;
        int             getCgiOutputFd() const;
        int             getCgiInputFd() const;
        const Request&  getRequest() const;
        bool            getCloseAfterResponse() const;
        ConnectionState getState() const;
        void startCgi(const CgiInfo &cgiInfo);
        ConnectionStatus readFromCGIPipe();
        ConnectionStatus checkCgiChild();
        bool            cgiCompletionIsPending() const;
        bool            cgiAbortIsPending() const;
        bool            cgiHasTimedOut() const;
        CgiCleanupStatus abortCgi();
        void            closeClientFd();
        void            resetCgiProcess();
        void            resetForNewRequest();
        bool            hasPendingResponse() const;
        bool            hasBufferedInput() const;
        bool            headerDeadlineExpired() const;
        bool            clientHasTimedOut() const;
        ConnectionStatus writeToCGIPipe();
        void            applyConnectionClosePolicy();


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
        Listener        *listener_;//non-owning pointer; Server owns the Listener
        ServerConfig    *defaultConfig_;//default for the listener;
        ServerConfig    *selectedConfig_;//starts as default then later selected based on Request host header
        std::time_t     lastActivity_;
        std::time_t     headersStart_;
        ConnectionState state_;

        void            refreshActivity();
};

#endif
