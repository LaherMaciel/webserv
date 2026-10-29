
#include "Connection.hpp"
#include "webserv.hpp"
#include "Response.hpp"
#include "ServerConfig.hpp"
#include "Server.hpp"
#include <unistd.h>
#include <iostream>
#include <sys/socket.h> //for recv() send()

Connection::Connection(int fd, const std::string &remoteAddress, Listener *listener, ServerConfig *defaultConfig)
                    : fd_(fd), in_buffer_(""), out_buffer_(""), bytes_sent_(0)
                    , closeAfterResponse_(true), remoteAddress_(remoteAddress)
                    , listener_(listener), defaultConfig_(defaultConfig), selectedConfig_(defaultConfig)
                    , serverConfigSelected_(false)
                    , lastActivity_(std::time(NULL)), headersStart_(std::time(NULL))
                    , bodyStart_(std::time(NULL))
                    , state_(CONN_NEW) {}

Connection::~Connection() { closeClientFd(); }

int Connection::getFd() const { return fd_; }

Listener *Connection::getListener() const { return listener_; }

ServerConfig *Connection::getSelectedConfig() const { return selectedConfig_; }

void Connection::setSelectedConfig(ServerConfig *config)
{
    selectedConfig_ = config;
    serverConfigSelected_ = true;
}

int Connection::getCgiOutputFd() const { return cgiProcess_.getOutputFd(); }

int Connection::getCgiInputFd() const { return cgiProcess_.getInputFd(); }

bool Connection::getCloseAfterResponse() const { return closeAfterResponse_; }

void Connection::closeAfterCurrentResponse() { closeAfterResponse_ = true; }

ConnectionState Connection::getState() const { return state_; }

void Connection::closeClientFd()
{
    state_ = CONN_CLOSING;
    if (fd_ != -1)
    {
        close(fd_);
        fd_ = -1;
    }
}

const Request& Connection::getRequest() const { return request_; }

void Connection::resetForNewRequest()
{
    state_ = CONN_WAITING_FOR_REQUEST;
    refreshActivity();
    request_ = Request();
    parser_.reset();
    closeAfterResponse_ = true;
    selectedConfig_ = defaultConfig_;
    serverConfigSelected_ = false;
}

bool Connection::hasPendingResponse() const { return !out_buffer_.empty(); }

bool Connection::hasBufferedInput() const { return !in_buffer_.empty(); }

void Connection::refreshActivity() { lastActivity_ = std::time(NULL); }

//Absolute header deadline protects against Slowloris-style clients
bool Connection::headerDeadlineExpired() const
{
    if (state_ != CONN_NEW && state_ != CONN_READING_HEADERS)
        return false;
    return std::difftime(std::time(NULL), headersStart_) >= CLIENT_HEADER_DEADLINE_SECONDS;
}

bool Connection::bodyDeadlineExpired() const
{
    if (state_ != CONN_READING_BODY)
        return false;
    return std::difftime(std::time(NULL), bodyStart_) >= CLIENT_BODY_DEADLINE_SECONDS;
}

bool Connection::clientHasTimedOut() const
{
    if (state_ == CONN_RUNNING_CGI || state_ == CONN_READY_TO_ROUTE || state_ == CONN_CLOSING)
        return false;
    return std::difftime(std::time(NULL), lastActivity_) >= CLIENT_IDLE_TIMEOUT_SECONDS;
}

void Connection::startCgi(const CgiInfo &cgiInfo, const std::vector<int> &inheritedFds)
{
    state_ = CONN_RUNNING_CGI;
    cgiProcess_.startCgi(cgiInfo, request_, remoteAddress_, inheritedFds);
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

CgiCleanupStatus Connection::abortCgi()
{
    return cgiProcess_.abort();
}

bool Connection::cgiHasTimedOut() const
{
    return cgiProcess_.hasTimedOut();
}

bool Connection::checkCgiChild()
{
    if (!cgiProcess_.checkChild())
        return false;
    Response response(500, request_.getVersion());
    cgiProcess_.finishCgi(response);
    queueResponse(response);
    return true;
}

CgiReadStatus Connection::readFromCGIPipe()
{
    CgiReadStatus status = cgiProcess_.readFromPipe();
    if (status == CGI_READ_ERROR)
    {
        queueErrorResponse(500, request_.getVersion(),
            "Internal Server Error: CGI read error", "text/plain");
    }
    else if (status == CGI_OUTPUT_TOO_LARGE)
    {
        queueErrorResponse(500, request_.getVersion(),
            "Internal Server Error: CGI output too large", "text/plain");
    }
    return status;
}

CgiWriteStatus Connection::writeToCGIPipe()
{
    CgiWriteStatus status = cgiProcess_.writeToPipe(request_.getBody());
    if (status == CGI_WRITE_ERROR)
    {
        queueErrorResponse(500, request_.getVersion(), 
            "Internal Server Error: CGI write error", "text/plain");
    }
    return status;
}

SocketReadResult Connection::readFromSocket()
{
    char buffer[IO_CHUNK_SIZE];
    ssize_t bytes_received = recv(fd_, buffer, sizeof(buffer), 0);
    if (bytes_received == 0)
    {
        std::cout << "Client input closed (fd: " << fd_ << ")\n";
        return SOCKET_EOF;
    }
    else if (bytes_received < 0)
    {
        std::cerr << "Error receiving data from client (fd: " << fd_ << ")\n";
        return SOCKET_READ_ERROR;
    }
    else
    {
        refreshActivity();
        std::cout << "Received data:\n";
        std::cout.write(buffer, bytes_received) << "\n";
        in_buffer_.append(buffer, bytes_received);
    }
    return SOCKET_DATA_READ;
}

SendResult Connection::sendResponse()
{
    size_t bytes_left = out_buffer_.size() - bytes_sent_;
    if (bytes_left == 0)
    {
        state_ = closeAfterResponse_ ? CONN_CLOSING : CONN_WAITING_FOR_REQUEST;
        return SEND_COMPLETE;
    }
    ssize_t sent = send(fd_, out_buffer_.c_str() + bytes_sent_, bytes_left, 0);
    if (sent <= 0)
        return SEND_FAILED;
    refreshActivity();
    bytes_sent_ += sent;
    if (bytes_sent_ < out_buffer_.size())
        return SEND_PENDING;
    out_buffer_.clear();
    bytes_sent_ = 0;
    state_ = closeAfterResponse_ ? CONN_CLOSING : CONN_WAITING_FOR_REQUEST;
    return SEND_COMPLETE;
}

void Connection::queueResponse(Response &response)
{
    refreshActivity();
    state_ = CONN_WRITING_RESPONSE;
    if (closeAfterResponse_)
        response.setHeader("Connection", "close");
    out_buffer_ += response.serialize();
}

void Connection::queueErrorResponse(int code, std::string version, std::string body, std::string contentType)
{
    if (version.empty())
        version = "HTTP/1.1";
    std::cerr << httpReasonPhrase(code) << " (fd: " << fd_ << ")\n";
    Response response(code, version);
    response.setErrorPage(selectedConfig_->errorPages_);
    if (!body.empty())
        response.setBody(body, contentType);
    else
        response.setDefaultErrorBody();
    queueResponse(response);
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

bool Connection::serverConfigSelected() const
{
    return serverConfigSelected_;
}

void Connection::processRequestHeaders()
{
    if (state_ == CONN_NEW || state_ == CONN_WAITING_FOR_REQUEST)
    {
        state_ = CONN_READING_HEADERS;
        headersStart_ = std::time(NULL);
    }
    if (state_ != CONN_READING_HEADERS)
        return;
    try
    {
        parser_.parseRequest(in_buffer_, request_);
        if (parser_.status_ == PARSE_INCOMPLETE)
            return ;
        applyConnectionClosePolicy();
        in_buffer_.erase(0, parser_.endOfHeaders_);
        bodyStart_ = std::time(NULL);
        state_ = CONN_READING_BODY;
    }
    catch(int error)
    {
        closeAfterResponse_ = true;
        queueErrorResponse(error, request_.getVersion());
    }
}

void Connection::setRequestBodyLimit(size_t maxBodySize)
{
    parser_.setMaxBodySize(maxBodySize);
}

void Connection::processRequestBody()
{
    try
    {
        parser_.parseRequestBody(in_buffer_, request_);
        in_buffer_.erase(0, parser_.endOfBody_);
        if (!request_.getIsBodyComplete())
            return ;
        state_ = CONN_READY_TO_ROUTE;
        request_.printRequest();
    }
    catch(int error)
    {
        closeAfterResponse_ = true;
        queueErrorResponse(error, request_.getVersion());
    }
}

//TEST WITH CURL!!!
//curl -v http://127.0.0.1:8080/
//or nc still works you just can't get an OK response unless you use printf and sleep:
//(printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n'; sleep 1) | nc 127.0.0.1 8080
// (printf 'GET / HTTP/1.1\r\nHost: x\r\n\r\n'; sleep 1; printf 'GET /static HTTP/1.1\r\nHost: x\r\n\r\n'; sleep 1) | nc 127.0.0.1 8080
// (printf 'GET / HTTP/1.1\r\nHost: x\r\n\r\nGET / HTTP/1.1\r\nHost: x\r\n\r\n'; sleep 1) | nc 127.0.0.1 8080
