#ifndef CONNECTION_HPP
# define CONNECTION_HPP

#include <iostream>
#include <string>
# include <map>

#define MAX_HEADER_SIZE 1000
#define MAX_URL_SIZE 2083

struct  Request
{
    std::string method;
    std::string url;
    std::string version;
    std::map<std::string, std::string> header;
    std::string body;
    int         code;
    size_t      bufferSize;
};

class Response;
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
        int         handleRequest();
        int         receiveRequest();
        int         sendResponse(int code);
        int         sendResponse(std::string &response);
        void        clearBuffer();
        void        ParseStartLine(Request &request);
        void        ParseHeader(Request &request);
        Request     ParseBody(Request &request);
        void        RequestParsing(Request &request);
        void        ParseMethod(Request &request, std::string startLine, int pos);
        void        organizeRequest();
        void        ParseUrl(Request &request, std::string startLine, int pos);
        void        requestError(int code, Request &request);
};

    std::string responseMessage(Response response);
    std::string toString(size_t n);
    Request     initStruct();

#endif