#ifndef CONNECTION_HPP
# define CONNECTION_HPP

#include <iostream>
#include <string>
# include <map>

#define MAX_HEADER_SIZE 1000
//why a std::string and not. char like the previews?

struct  Request
{
    std::string method;
    std::string url;
    std::string version;
    std::map<std::string, std::string> header;
    std::string body;
    size_t      bufferSize;
};

class Connection
{
    private: //prohibits copy construct or copy assign, so we don't need to create functions
        int         _fd;
        std::string in_buffer;

        Connection(const Connection& other);
        Connection& operator=(const Connection& other);

    public:
        Connection();
        Connection(int fd);
        ~Connection();
        int     handleRequest();
        int     receiveRequest();
        int     sendResponse(int code);
        void    clearBuffer();
        Request ParseRequestLine();
        Request    ParseHeader(Request request);
        Request    ParseBody(Request request);
        int     RequestParsing();
};

#endif