
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
        std::cout << "Received data:\n" << buffer << "\n";
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
    if (in_buffer.size() > MAX_HEADER_SIZE)
    {
        sendResponse(431);
        std::cerr << "Request header too large, closing connection (fd: " << _fd << ")\n";
        return -1;
    }
    if (in_buffer.find("\r\n\r\n") != std::string::npos)
        sendResponse(200);
    else//just for debug
    {
        std::cout << "Waiting for end of headers, current in_buffer size: "
                    << in_buffer.size() << std::endl;
    }
    return 0;
}
//TEST WITH CURL!!!
//curl -v http://127.0.0.1:8080/
//or nc still works you just can't get an OK response unless you use printf and sleep:
//(printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n'; sleep 1) | nc 127.0.0.1 8080