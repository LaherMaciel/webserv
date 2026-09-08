
#include "Connection.hpp"
#include <unistd.h>
#include <iostream>
#include <sys/socket.h> //for recv() send()
#include <cerrno>//for errno

Connection::Connection() : _fd(-1), in_buffer("") {}

Connection::Connection(int fd) : _fd(fd), in_buffer("") {}

Connection::~Connection()
{
    close(_fd);
}

int Connection::receiveRequest()
{
    char buffer[1024];
    ssize_t bytes_received = recv(_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received == 0)
    {
        std::cout << "Client disconnected (fd: " << _fd << ")\n";
        return -1;
    }
    else if (bytes_received < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        std::cerr << "Error receiving data from client (fd: " << _fd << ")\n";
        return -1;
    }
    else
    {
        buffer[bytes_received] = '\0';
        in_buffer.append(buffer, bytes_received);
    }
    return 0;
}

int Connection::sendResponse(int code)
{
    std::string response;
    if (code == 200)
        response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    else if (code == 431)
        response = "HTTP/1.1 431 Request Header Fields Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    else
        return -1;
    ssize_t bytes_sent = send(_fd, response.c_str(), response.length(), 0);
    if (bytes_sent < 0)
        return -1;
    //placeholder to handle partial sends
    return 0;
}

int Connection::handleRequest()
{
    std::cout << "Handling client connection (fd: " << _fd << ")\n";
    if (receiveRequest() == -1)
        return -1;
    std::cout << "Received data:\n" << in_buffer << "\n";
    if (in_buffer.size() > MAX_HEADER_SIZE)
    {
        sendResponse(431);
        std::cerr << "Request header too large, closing connection (fd: " << _fd << ")\n";
        std::cout << std::endl << std::endl << in_buffer << std::endl;
        return -1;
    }
    if (in_buffer.find("\r\n\r\n") != std::string::npos)
    {
        if (RequestParsing() != -1) // I still need to clean the code of receiving and storing the request before starting the actuall parcing
            sendResponse(200);
    }
    else//just for debug
    {
        std::cout << "Waiting for end of headers, current in_buffer size: "
                    << in_buffer.size() << std::endl;
    }
    return 0;
}

void    Connection::clearBuffer()
{
    in_buffer = "";
}

Request    Connection::ParseRequestLine()
{
    Request     request;
    std::string requestLine;
    size_t      i = 0;
    size_t      pos;

    pos = in_buffer.find("\r\n");
    requestLine = in_buffer.substr(0, pos);
    while (i < 3)
    {
        if (i < 2)
           pos = requestLine.find(" ");
        else
            pos = requestLine.size();
        if (pos == std::string::npos)
            throw ;
        switch (i)
        {
            case 0:
                request.method = requestLine.substr(0, pos);
                break ;
            case 1:
                request.url = requestLine.substr(0, pos);
                break ;
            case 2:
                request.version = requestLine.substr(0, pos);
                break ;
        }
        requestLine = requestLine.erase(0, pos + 1);
        i++;
    }
    in_buffer = in_buffer.erase(0, in_buffer.find("\r\n") + 2);
    return (request);
}

Request    Connection::ParseHeader(Request request)
{
    std::string header;
    std::string key;
    size_t      pos;
    size_t      linelen;

    linelen = in_buffer.find("\r\n\r\n");
    if (linelen == std::string::npos)
            throw ;
    header = in_buffer.substr(0, linelen + 2);
    while (header.size() > 0)
    {
        linelen = header.find("\r\n");
        if (linelen == std::string::npos)
            throw ;
        std::string line = header.substr(0, linelen);
        pos = line.find(": ");
        if (pos > linelen)
            throw ;
        key = line.substr(0, pos);
        request.header[key] = line.substr(pos + 2, linelen - (pos + 2));
        header = header.erase(0, linelen + 2);
    }
    in_buffer = in_buffer.erase(0, in_buffer.find("\r\n\r\n") + 4);
    return (request);
}

Request    Connection::ParseBody(Request request)
{
    return (request);
}

static void    printRequest(Request request)
{
    std::map<std::string, std::string>::const_iterator it;

    std::cout << std::endl << std::endl;
    std::cout << "METHOD: " << request.method << "  URL: " << request.url << "  VERSION: " << request.version << std::endl;
    std::cout << "HEADER: " << std::endl;
    for (it = request.header.begin(); it != request.header.end(); ++it)
    {
        std::cout << "KEY: " << it->first << "  |  BODY: " << it->second << std::endl;
    }
    std::cout << "HEADER BODY:  " << request.body << std::endl;
}

int    Connection::RequestParsing()
{
    Request request;

    try
    {
        request = ParseRequestLine();
        request = ParseHeader(request);
        request = ParseBody(request);
        printRequest(request);
        std::cout << "END RECEIVED OF REQUEST" << std::endl << std::endl << std::endl;
    }
    catch(std::exception& e)
    {
        std::cerr << e.what() << '\n';
        sendResponse(400);
        return (-1);
    }
    return (0);
}

//TEST WITH CURL!!!
//curl -v http://127.0.0.1:8080/
//or nc still works you just can't get an OK response unless you use printf and sleep:
//(printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n'; sleep 1) | nc 127.0.0.1 8080