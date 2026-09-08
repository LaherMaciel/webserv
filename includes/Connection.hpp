#ifndef CONNECTION_HPP
# define CONNECTION_HPP

#include <iostream>
#include <string>
# include <map>

#define MAX_HEADER_SIZE 1000

//ERROR HANDLING OPTION A
#define NOT_FOUND "404 Not Found\r\n"
#define OK "200 OK"
#define BAD_REQUEST "400 Bad Request\r\n"
#define REQUEST_HEADER_FILE_TOO_LONG "431 Request Header Fields Too Large\r\n"
#define INTERNAL_SERVER_ERROR "500 Internal Server Error\r\n"

//why a std::string and not. char like the previews?

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
        int        handleRequest();
        int        receiveRequest();
        int        sendResponse(int code);
        int        sendResponse(std::string &response);
        void       clearBuffer();
        Request    ParseStartLine(Request &request);
        Request    ParseHeader(Request &request);
        Request    ParseBody(Request &request);
        Request    RequestParsing(Request &request);
        Request    ParseMethod(Request &request, std::string startLine, int pos);
        // the information of why this exception is here is not the Connection.cpp line 113 :)
        /* class HTTPExceptions : public std::exception
		{
			public:
				virtual const char* what() const throw();
		}; */
};

    std::string responseMessage(Response response);
    std::string toString(size_t n);
    Request     initStruct();

#endif