
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
    std::string response = statusText(code);
    if (code == 200)
        response = "HTTP/1.1 " + response + "Content-Length: 0\r\n\r\n";
    else if (code == 431)
        response = "HTTP/1.1 " + response + "Content-Length: 0\r\nConnection: close\r\n\r\n";
    else if (code == 404)
        response = "HTTP/1.1 " + response + "\r\n";
    else if (code == 501)
        response = "HTTP/1.1 " + response + "\r\n";
    else
        return -1;
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

/**
 * OK, because I'm a blockhead, I decided to send everything by reference
 * and it works, but the protection isn't the best code-wise. So later on
 * I need to improve the protection. What I mean by this is: the code
 * works, but to work properly we need to be mindful about the order of
 * the error and throws, because if we throw before, for example,
 * assigning the code value to request.code, it will silently have the
 * wrong behavior. So later on I'll try to improve it so that even if
 * something like that happens by mistake, the code still works ok or
 * breaks loudly so we know something is wrong, instead of this possible
 * incorrect silent behavior. The easiest solution would be to just swap
 * the while loop and try/catch order - having the try/catch (that's
 * inside requestParsing()) outside the while loop (the handleRequest()
 * while loop). But I don't know how I feel about that....
 */
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
    while (in_buffer.find("\r\n\r\n") != std::string::npos)
    {
        Request request = initStruct();
        request = RequestParsing(request);
        if (request.code == 200) // I still need to clean the code of receiving and storing the request before starting the actuall parcing
        {
            Response response;
            response.buildResponse(request);
            std::string respMessage = response.getResponse();
            sendResponse(respMessage);
        }
    }
    /* else//just for debug
    {
        std::cout << "Waiting for end of headers, current in_buffer size: "
                    << in_buffer.size() << std::endl;
    } */
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