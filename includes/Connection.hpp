#ifndef CONNECTION_HPP
# define CONNECTION_HPP

#include <iostream>
#include <string>
#include "RequestParser.hpp"

#define MAX_HEADER_SIZE 1000

class Connection
{
	public:
        Connection();
        Connection(int fd);
        ~Connection();
        int handleRequest();
        int receiveRequest();
        int sendResponse(int code);
    private:
        int			fd_;
        std::string	in_buffer_;
        Connection(const Connection& other);
        Connection& operator=(const Connection& other);
        RequestParser	request_;
};

#endif