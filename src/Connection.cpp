
#include "Connection.hpp"
#include "webserv.hpp"
#include "Response.hpp"
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
        /* // ! the subject forbids using errno right after a read/write(). THe poll itself already tells use that the fd is readable
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0; */
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
    std::string response = statusText(code);
    if (code == 200)
        response = "HTTP/1.1 " + response + "\r\nContent-Length: 0\r\n\r\n";
    else if (code == 431)
        response = "HTTP/1.1 " + response + "\r\Content-Length: 0\r\nConnection: close\r\n\r\n";
    else
        response = "HTTP/1.1 " + response + "\r\n";
    ssize_t bytes_sent = send(_fd, response.c_str(), response.length(), 0);
    if (bytes_sent < 0)
        return -1;
    //placeholder to handle partial sends
    return 0;
}

int Connection::sendResponse(std::string &response)
{
    ssize_t bytes_sent = send(_fd, response.c_str(), response.length(), 0);
    if (bytes_sent < 0)
        return -1;
    return 0;
}

void Connection::organizeRequest()
{
    Request request = initStruct();
    try
    {
        while (in_buffer.find("\r\n\r\n") != std::string::npos)
        {
            request = initStruct();
            RequestParsing(request);
            if (request.code == 200)
            {
                Response response;
                response.buildResponse(request);
                std::string respMessage = response.getResponse();
                sendResponse(respMessage);
            }
        }
    }
    catch(std::exception& e)
    {
        std::cerr << e.what() << '\n';
        in_buffer.erase(0, in_buffer.size());
        if (request.code != 200)
            sendResponse(request.code);
        else
            sendResponse(1);
    }
}

/* else//just for debug
    {
        std::cout << "Waiting for end of headers, current in_buffer size: "
                    << in_buffer.size() << std::endl;
    } */
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
    organizeRequest();
    return 0;
}

void    Connection::clearBuffer()
{
    in_buffer.erase(0, in_buffer.size());
}

//TEST WITH CURL!!!
//curl -v http://127.0.0.1:8080/
//or nc still works you just can't get an OK response unless you use printf and sleep:
//(printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n'; sleep 1) | nc 127.0.0.1 8080

/**
 * I'm having problems with the loop of handleRequest in the case where we
 * receive two requests in one go, and one of them fails. I want to make a clean
 * exit where it just stops where it was and prints the error both to the
 * terminal and to the client. But at the moment my brain isn't braining so... I
 * don't know what to do AT THE MOMENT. The problem is that, as it is now, it
 * prints just fine to the terminal, but it doesn't stop the loop and doesn't
 * print to the client, because it's losing the error code. I feel like the
 * solution is pretty simple on its own, this exception might not even be
 * necessary, but for now I can't think of the solution/fix for it.
 */
/* const char *Connection::HTTPExceptions::what() const throw()
{
	return ("");
} */