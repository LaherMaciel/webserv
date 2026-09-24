
#include "Connection.hpp"
#include "webserv.hpp"
#include "Response.hpp"
#include <unistd.h>
#include <iostream>
#include <sys/socket.h> //for recv() send()
#include <cerrno>//for errno

Connection::Connection() : fd_(-1), in_buffer_(""), out_buffer_(""), bytes_sent_(0) {}

Connection::Connection(int fd) : fd_(fd), in_buffer_(""), out_buffer_(""), bytes_sent_(0) {}

Connection::~Connection() { close(fd_); }

const Request& Connection::getRequest() const { return request_; }

const CgiProcess& Connection::getCgiProcess() const { return cgiProcess_; }

void Connection::startCgi(const CgiInfo &cgiInfo, Response &response)
{
    cgiProcess_.startCgi(cgiInfo, request_, response);
}

int Connection::readFromSocket()
{
    char buffer[IO_CHUNK_SIZE];
    ssize_t bytes_received = recv(fd_, buffer, sizeof(buffer), 0);
    if (bytes_received == 0)
    {
        std::cout << "Client disconnected (fd: " << fd_ << ")\n";
        return -1;
    }
    else if (bytes_received < 0)
    {
        std::cerr << "Error receiving data from client (fd: " << fd_ << ")\n";
        return -1;
    }
    else
    {
        std::cout << "Received data:\n";
        std::cout.write(buffer, bytes_received) << "\n";
        in_buffer_.append(buffer, bytes_received);
    }
    return 0;
}

ConnectionStatus Connection::sendResponse()
{
    size_t bytes_left = out_buffer_.size() - bytes_sent_;
    ssize_t sent = send(fd_, out_buffer_.c_str() + bytes_sent_, bytes_left, 0);
    if (sent <= 0)
        return CLOSE_CONNECTION;
    bytes_sent_ += sent;
    if (bytes_sent_ < out_buffer_.size())
        return WAIT_FOR_MORE;
    return CLOSE_CONNECTION;
}

void Connection::queueResponse(const Response &response)
{
    out_buffer_ = response.serialize();
    bytes_sent_ = 0;
}

ConnectionStatus Connection::queueErrorResponse(int code, std::string version)
{
    if (version.empty())
        version = "HTTP/1.1";
    std::cerr << httpReasonPhrase(code) << " (fd: " << fd_ << ")\n";
    Response response(code, version);
    queueResponse(response);
    return RESPONSE_READY;
}

ConnectionStatus Connection::handleRequest()
{
    std::cout << "Handling client connection (fd: " << fd_ << ")\n";
    if (readFromSocket() == -1)
        return CLOSE_CONNECTION;
    if (in_buffer_.size() > MAX_HEADER_SIZE)
        return queueErrorResponse(431);
    try
    {
        if (request_.getMethod().empty())
            parser_.parseRequest(in_buffer_, request_);
        in_buffer_.erase(0, parser_.endOfHeaders_);
        //if (in_buffer_.find("\r\n\r\n"))
        //if (request_.getMethod() == "POST")
        //parser_.parseBody(in_buffer_ + endofheaders_, request_)
    }
    catch(int error)
    {
        return queueErrorResponse(error, request_.getVersion());
    }
    if (parser_.status_ == PARSE_INCOMPLETE)
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
