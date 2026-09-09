#ifndef CONNECTION_HPP
# define CONNECTION_HPP

#include <iostream>
#include <string>
#include "RequestParser.hpp"
#include "Request.hpp"

#define MAX_HEADER_SIZE 1000

enum ConnectionStatus
{
    CLOSE_CONNECTION,
    WAIT_FOR_MORE,
    REQUEST_READY
};

class Connection
{
	public:
        Connection();
        Connection(int fd);
        ~Connection();
        ConnectionStatus handleRequest();
        int readFromSocket();
        int sendResponse(int code);
        const Request& getRequest() const;

    private:
        int			fd_;
        std::string	in_buffer_;
        Connection(const Connection& other);
        Connection& operator=(const Connection& other);
        RequestParser	parser_;
        Request         request_;

};

#endif