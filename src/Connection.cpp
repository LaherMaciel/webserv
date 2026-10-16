
#include "Connection.hpp"
#include "webserv.hpp"
#include "Response.hpp"
#include <unistd.h>
#include <iostream>
#include <sys/socket.h> //for recv() send()
#include <cerrno>//for errno

Connection::Connection(int fd, const std::string &remoteAddress)
                    : fd_(fd), in_buffer_(""), out_buffer_(""), bytes_sent_(0)
                    , closeAfterResponse_(true), remoteAddress_(remoteAddress) {}

Connection::~Connection() { closeClientFd(); }

int Connection::getFd() const { return fd_; }

int Connection::getCgiOutputFd() const { return cgiProcess_.getOutputFd(); }

int Connection::getCgiInputFd() const { return cgiProcess_.getInputFd(); }

bool Connection::getCloseAfterResponse() const { return closeAfterResponse_; }

void Connection::closeClientFd()
{
    if (fd_ != -1)
    {
        close(fd_);
        fd_ = -1;
    }
}

const Request& Connection::getRequest() const { return request_; }

void Connection::resetRequest()
{
    request_ = Request();
}

void Connection::resetForNewRequest()
{
    request_ = Request();
    parser_.reset();
    closeAfterResponse_ = true;
}

bool Connection::hasPendingResponse() const { return !out_buffer_.empty(); }

bool Connection::hasBufferedInput() const { return !in_buffer_.empty(); }

void Connection::startCgi(const CgiInfo &cgiInfo)
{
    cgiProcess_.startCgi(cgiInfo, request_, remoteAddress_);
}

void Connection::resetCgiProcess()
{
    cgiProcess_.reset();
}

bool Connection::cgiCompletionIsPending() const
{
    return cgiProcess_.completionIsPending();
}

bool Connection::cgiAbortIsPending() const
{
    return cgiProcess_.abortIsPending();
}

bool Connection::cgiIsRunning() const
{
    return cgiProcess_.isRunning();
}

CgiCleanupStatus Connection::abortCgi()
{
    return cgiProcess_.abort();
}

bool Connection::cgiHasTimedOut() const
{
    return cgiProcess_.hasTimedOut();
}

ConnectionStatus Connection::checkCgiChild()
{
    if (!cgiProcess_.checkChild())
        return CGI_WAITING_FOR_EXIT;
    Response response(500, request_.getVersion());
    cgiProcess_.finishCgi(response);
    queueResponse(response);
    return RESPONSE_READY;
}

ConnectionStatus Connection::readFromCGIPipe()
{
    CgiReadStatus status = cgiProcess_.readFromPipe();
    if (status == CGI_OUTPUT_COMPLETE)
        return checkCgiChild();
    else if (status == CGI_READ_ERROR)
    {
        queueErrorResponse(500, request_.getVersion(),
            "Internal Server Error: CGI read error", "text/plain");
        return CGI_IO_ERROR;
    }
    else if (status == CGI_OUTPUT_TOO_LARGE)
    {
        queueErrorResponse(500, request_.getVersion(),
            "Internal Server Error: CGI output too large", "text/plain");
        return CGI_IO_ERROR;
    }
    return WAIT_FOR_MORE;
}

ConnectionStatus Connection::writeToCGIPipe()
{
    CgiWriteStatus status = cgiProcess_.writeToPipe(request_.getBody());
    if (status == CGI_INPUT_COMPLETE)
        return CGI_STARTED;
    else if (status == CGI_WRITE_ERROR)
    {
        queueErrorResponse(500, request_.getVersion(), 
            "Internal Server Error: CGI write error", "text/plain");
        return CGI_IO_ERROR;
    }
    return WAIT_FOR_MORE;
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
    if (bytes_left == 0)
        return RESPONSE_SENT;
    ssize_t sent = send(fd_, out_buffer_.c_str() + bytes_sent_, bytes_left, 0);
    if (sent <= 0)
        return CLOSE_CONNECTION;
    bytes_sent_ += sent;
    if (bytes_sent_ < out_buffer_.size())
        return WAIT_FOR_MORE;
    out_buffer_.clear();
    bytes_sent_ = 0;
    return RESPONSE_SENT;
}

void Connection::queueResponse(Response &response)
{
    if (closeAfterResponse_)
        response.setHeader("Connection", "close");
    out_buffer_ += response.serialize();
}

ConnectionStatus Connection::queueErrorResponse(int code, std::string version, std::string body, std::string contentType)
{
    if (version.empty())
        version = "HTTP/1.1";
    std::cerr << httpReasonPhrase(code) << " (fd: " << fd_ << ")\n";
    Response response(code, version);
    if (!body.empty())
        response.setBody(body, contentType);
    else
        response.setDefaultErrorBody();
    queueResponse(response);
    return RESPONSE_READY;
}

void Connection::applyConnectionClosePolicy()
{
    if (request_.getVersion() == "HTTP/1.1")
        closeAfterResponse_ = false;
    std::map<std::string, std::string> headers = request_.getHeaders();
    std::map<std::string, std::string>::iterator it = headers.find("connection");
    if (it != headers.end())
    {
        std::string value = toLower(it->second);
        if (value == "close")
            closeAfterResponse_ = true;
    }
}

//merged the 2 try/catch blocks into 1
ConnectionStatus Connection::handleRequest()
{
    std::cout << "Handling client connection (fd: " << fd_ << ")\n";
    try
    {
        if (request_.getMethod().empty())
        {
            parser_.parseRequest(in_buffer_, request_);
            if (parser_.status_ == PARSE_INCOMPLETE)
                return WAIT_FOR_MORE;
            applyConnectionClosePolicy();
            in_buffer_.erase(0, parser_.endOfHeaders_);
        }
        parser_.parseRequestBody(in_buffer_, request_);
        in_buffer_.erase(0, parser_.endOfBody_);
        if (!request_.getIsBodyComplete())
            return WAIT_FOR_MORE;
    }
    catch(int error)
    {
        closeAfterResponse_ = true;
        return queueErrorResponse(error, request_.getVersion());
    }
    request_.printRequest();
    return REQUEST_READY;
}

//TEST WITH CURL!!!
//curl -v http://127.0.0.1:8080/
//or nc still works you just can't get an OK response unless you use printf and sleep:
//(printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n'; sleep 1) | nc 127.0.0.1 8080
// (printf 'GET / HTTP/1.1\r\nHost: x\r\n\r\n'; sleep 1; printf 'GET /static HTTP/1.1\r\nHost: x\r\n\r\n'; sleep 1) | nc 127.0.0.1 8080
// (printf 'GET / HTTP/1.1\r\nHost: x\r\n\r\nGET / HTTP/1.1\r\nHost: x\r\n\r\n'; sleep 1) | nc 127.0.0.1 8080
