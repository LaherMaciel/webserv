
#include "Connection.hpp"
#include "webserv.hpp"
#include "Response.hpp"
#include <unistd.h>
#include <iostream>
#include <sys/socket.h> //for recv() send()
#include <cerrno>//for errno

Connection::Connection() : fd_(-1), in_buffer_("") {}

Connection::Connection(int fd) : fd_(fd), in_buffer_("") {}

Connection::~Connection() { close(fd_); }

const Request& Connection::getRequest() const { return request_; }

int Connection::readFromSocket()
{
    char buffer[1024];
    ssize_t bytes_received = recv(fd_, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received == 0)
    {
        std::cout << "Client disconnected (fd: " << fd_ << ")\n";
        return -1;
    }
    else if (bytes_received < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        std::cerr << "Error receiving data from client (fd: " << fd_ << ")\n";
        return -1;
    }
    else
    {
        buffer[bytes_received] = '\0';
        std::cout << "Received data:\n" << buffer << "\n";
        in_buffer_.append(buffer, bytes_received);
    }
    return 0;
}

int Connection::sendResponse(Response &response)
{
    std::string response_str = response.serialize();
    ssize_t bytes_sent = send(fd_, response_str.c_str(), response_str.length(), 0);
    if (bytes_sent < 0)
        return -1;
    //placeholder to handle partial sends
    return 0;
}

int Connection::sendErrorResponse(int code)
{
    std::cerr << httpReasonPhrase(code) << " (fd: " << fd_ << ")\n";
    Response response(code);
    return sendResponse(response);
}

ConnectionStatus Connection::handleRequest()
{
    std::cout << "Handling client connection (fd: " << fd_ << ")\n";
    if (readFromSocket() == -1)
        return CLOSE_CONNECTION;
    if (in_buffer_.size() > MAX_HEADER_SIZE)
    {
        sendErrorResponse(431);
        return CLOSE_CONNECTION;
    }
    ParseStatus status = parser_.parseRequest(in_buffer_, request_);
    if (status == PARSE_ERROR)
    {
        sendErrorResponse(parser_.getErrorCode());
        return CLOSE_CONNECTION;
    }
    else if (status == PARSE_INCOMPLETE)
    {
        std::cout << "Waiting for end of headers, current in_buffer size: "
                    << in_buffer_.size() << std::endl;
        return WAIT_FOR_MORE;
    }
    request_.printRequest();
    return REQUEST_READY;
}
//TEST WITH CURL!!!
//curl -v http://127.0.0.1:8080/
//or nc still works you just can't get an OK response unless you use printf and sleep:
//(printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n'; sleep 1) | nc 127.0.0.1 8080
