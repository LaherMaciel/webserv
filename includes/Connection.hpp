#ifndef CONNECTION_HPP
# define CONNECTION_HPP

#include <iostream>
#include <string>
#include "RequestParser.hpp"
#include "Request.hpp"

class Response;

enum ConnectionStatus
{
    CLOSE_CONNECTION,
    WAIT_FOR_MORE,
    REQUEST_READY,
    RESPONSE_READY,
    CONTINUE_CONNECTION
};

class Connection
{
    public:
        Connection();
        Connection(int fd);
        ~Connection();
        ConnectionStatus handleRequest();
        int             readFromSocket();
        void            queueResponse(const Response &response);
        ConnectionStatus sendResponse();
        ConnectionStatus queueErrorResponse(int code, std::string version = "HTTP/1.1");
        const Request&  getRequest() const;
        void            resetRequest();
        bool            hasPendingResponse() const;

    private:
        int             fd_;
        std::string	    in_buffer_;
        std::string     out_buffer_;
        size_t          bytes_sent_;
        Connection(const Connection& other);
        Connection& operator=(const Connection& other);
        RequestParser   parser_;
        Request         request_;
};

#endif