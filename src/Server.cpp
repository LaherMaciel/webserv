#include "webserv.hpp"
#include "Server.hpp"
#include "Connection.hpp"
#include "Response.hpp"
#include "Router.hpp"
#include "ServerConfig.hpp"
#include <map>
#include <cstring>//for memset
#include <sys/socket.h>//for socket(), bind(), listen(), accept()
#include <netinet/in.h>//for sockaddr_in
#include <unistd.h>//for close
#include <iostream>
#include <poll.h>//for poll()
#include <stdexcept>//for exception types
#include <vector>
#include <netdb.h>

static const int POLL_TIMEOUT_MS = 100;

volatile sig_atomic_t Server::shutdownRequested_ = 0;

Listener::Listener(ServerConfig *config)
    : fd_(-1), host_(config->host_), port_(config->port_)
{
    configs_.push_back(config);
}

Server::Server(const std::vector<ServerConfig>& config, const std::string &logPath, Log::Level level)
    : log_(logPath, level), config_(config)
{
    shutdownRequested_ = 0;
    log_.record(Log::INFO, "Server", "Loaded " + toString(config_.size()) + " server blocks");
}

Server::~Server()
{
    for (size_t i = 0; i < listeners_.size(); ++i)
    {
        if (listeners_[i].fd_ != -1)
        {
            close(listeners_[i].fd_);
            listeners_[i].fd_ = -1;
        }
    }
    for (std::map<int, Connection *>::iterator it = conns_.begin(); it != conns_.end(); ++it)
        delete it->second;
    for (size_t i = 0; i < closingConnections_.size(); ++i)
        delete closingConnections_[i];
    conns_.clear();
    closingConnections_.clear();
    cgiOwners_.clear();
    poll_fds_.clear();
    log_.flush();
}

void Server::requestShutdown(int signal)
{
    (void)signal;
    shutdownRequested_ = 1;
}

void Server::addFdToPoll(int fd, short events)
{
    struct pollfd entry;
    entry.fd = fd;
    entry.events = events;
    entry.revents = 0;
    poll_fds_.push_back(entry);
}

int    Server::acceptConnection(std::string &remoteAddress, size_t pollfd_pos)
{
    sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    std::memset(&clientAddr, 0, sizeof(clientAddr));

    int clientfd = accept(poll_fds_[pollfd_pos].fd, reinterpret_cast<sockaddr*>(&clientAddr), &clientAddrLen);

    if (clientfd < 0)
    {
        log_.record(Log::ERROR, "Server", "Failed to accept a connection");
        return (-1);
    }
    if (set_non_blocking(clientfd) < 0)
    {
        log_.record(Log::ERROR, "Server", "Failed to make a client socket non-blocking");
        close(clientfd);
        return (-1);
    }
    remoteAddress = ipv4LongToString(ntohl(clientAddr.sin_addr.s_addr));
    return (clientfd);
}

static void cleanCgiFd(int cgiFd, std::map<int, Connection *> &cgiOwners, std::vector<struct pollfd> &poll_fds)
{
    if (cgiFd == -1)
        return;
    cgiOwners.erase(cgiFd);
    for (size_t j = 0; j < poll_fds.size(); ++j)
    {
        if (poll_fds[j].fd == cgiFd)
        {
            poll_fds[j].fd = -1;
            poll_fds[j].events = 0;
            break;
        }
    }
}

void Server::cleanDeadFds(std::vector<int> &deadfds)
{
    for (size_t i = deadfds.size(); i > 0; --i)
    {
        int deadFd = deadfds[i - 1];
        std::map<int, Connection *>::iterator it = conns_.find(deadFd);
        if (it != conns_.end())
        {
            Connection *conn = it->second;
            cleanCgiFd(conn->getCgiOutputFd(), cgiOwners_, poll_fds_);
            cleanCgiFd(conn->getCgiInputFd(), cgiOwners_, poll_fds_);
            conns_.erase(it);
            conn->closeClientFd();
            if (conn->abortCgi() == CGI_CLEANUP_DONE)
                delete conn;
            else
                closingConnections_.push_back(conn);
        }
        for (size_t j = poll_fds_.size(); j > 0; --j)
        {
            if (poll_fds_[j - 1].fd == deadFd)
                poll_fds_.erase(poll_fds_.begin() + (j - 1));
        }
    }
    for (size_t i = poll_fds_.size(); i > 0; --i)
    {
        if (poll_fds_[i - 1].fd == -1)
            poll_fds_.erase(poll_fds_.begin() + (i - 1));
    }
}

//AF_INET = IPv4, SOCK_STREAM = TCP, 0 = default protocol (TCP for SOCK_STREAM)
void Server::initSocket(Listener &listener)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        throw std::runtime_error("Failed to create socket");
    }
    listener.fd_ = fd;
    if (set_non_blocking(fd) < 0)
    {
        throw std::runtime_error("Failed to set socket to non-blocking");
    }
    int opt_active = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt_active, sizeof(opt_active)) < 0)
    {
        throw std::runtime_error("Failed to set socket options");
    }
}

void Server::bindSocket(Listener &listener)
{
    struct addrinfo hints;
    struct addrinfo *result;
    
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; //IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    std::string port = toString(listener.port_);

    int status = getaddrinfo(listener.host_.c_str(), port.c_str(), &hints, &result);
    if (status != 0)
        throw std::runtime_error(gai_strerror(status));
    int bindResult = bind(listener.fd_, result->ai_addr, result->ai_addrlen);//bind socket with address/port
    freeaddrinfo(result);
    if (bindResult < 0)
        throw std::runtime_error("Failed to bind socket");
    if (listen(listener.fd_, MAX_PENDING_CONNECTIONS) < 0)//set socket to listen for incoming connections
        throw std::runtime_error("Failed to listen on socket");
}

bool Server::addToExistingListener(ServerConfig *config)
{
    for (size_t i = 0; i < listeners_.size(); i++)
    {
        if (config->port_ == listeners_[i].port_ &&
            config->host_ == listeners_[i].host_)
        {
            listeners_[i].configs_.push_back(config);
            log_.record(Log::DEBUG, "Config", "Shared listener " + config->host_
                        + ":" + toString(config->port_));
            return (true);
        }
    }
    return (false);
}

void Server::startServer()
{
    for (size_t i = 0; i < config_.size(); i++)
    {
        ServerConfig *config = &config_[i];
        if (log_.enabled(Log::DEBUG))
        {
            for (size_t j = 0; j < config->locations_.size(); ++j)
            {
                const LocationConfig &location = config->locations_[j];
                log_.record(Log::DEBUG, "Config", "port=" + toString(config->port_)
                            + " location=" + location.path_.substr(0, 256)
                            + " body_limit=" + toString(location.maxBodySize_));
            }
        }
        if (!addToExistingListener(config))
        {
            Listener newListener(config);
            initSocket(newListener);
            bindSocket(newListener);
            addFdToPoll(newListener.fd_);
            listeners_.push_back(newListener);
            log_.record(Log::INFO, "Server", "Listening on " + newListener.host_
                        + ":" + toString(newListener.port_));
        }
    }  
    
}

Listener *Server::getListener(int fd)
{
    for (size_t i = 0; i < listeners_.size(); i++)
    {
        if (fd == listeners_[i].fd_)
            return &listeners_[i];
    }
    return NULL;
}

Connection *Server::getConnection(int fd)
{
    std::map<int, Connection *>::iterator it = conns_.find(fd);
    if (it == conns_.end())
        return NULL;
    return it->second;
}

ServerConfig *Server::selectServerConfig(Connection *connection)
{
    Listener *listener = connection->getListener();
    if (!listener)
    {
        throw std::runtime_error("Server error");
    }
    const std::map<std::string, std::string> headers = connection->getRequest().getHeaders();
    std::map<std::string, std::string>::const_iterator host = headers.find("host");
    if (host != headers.end())
    {
        std::string name = toLower(host->second);
        size_t pos = name.find(':');
        if (pos != std::string::npos)
            name = name.substr(0, pos);
        for (size_t i = 0; i < listener->configs_.size(); ++i)
        {
            for (size_t j = 0; j < listener->configs_[i]->serverName_.size(); ++j)
            {
                if (toLower(listener->configs_[i]->serverName_[j]) == name)//need to compare multiple names
                    return (listener->configs_[i]);
            }
        }
    }
    return (listener->configs_[0]);
}

void Server::routeConnectionRequest(Connection *conn, size_t pollfd_pos)
{
    Response response;
    CgiInfo cgiInfo;
    Router router(*conn->getSelectedConfig(), log_);
    RouteType result;
    try
    {
        result = router.routeRequest(conn->getRequest(), response, cgiInfo);
    }
    catch (int errorCode)
    {
        response = Response(errorCode, conn->getRequest().getVersion());
        response.setErrorPage(conn->getSelectedConfig()->errorPages_);
        if (!response.setDefaultErrorBody())
            log_.record(Log::WARN, "Response", "Custom error page unavailable for status="
                        + toString(errorCode) + " file="
                        + conn->getSelectedConfig()->errorPages_.find(errorCode)->second.substr(0, 256)
                        + " " + conn->getLogContext());
        result = ROUTE_ERROR;
    }
    if (result == ROUTE_CGI)
        startCgi(conn, cgiInfo, pollfd_pos);
    else
        conn->queueResponse(response);
}

void Server::processBufferedRequest(Connection *conn, size_t pollfd_pos)
{
    conn->processRequestHeaders();
    if (conn->getState() == CONN_READING_HEADERS)
        return ;
    if (conn->getState() == CONN_READING_BODY && !conn->serverConfigSelected())
    {
        conn->setSelectedConfig(selectServerConfig(conn));
        Router router(*conn->getSelectedConfig(), log_);
        const LocationConfig *location = router.findLocation(conn->getRequest().getPath());
        if (!location)
        {
            conn->closeAfterCurrentResponse();
            conn->queueErrorResponse(404, conn->getRequest().getVersion());
        }
        else
            conn->setRequestBodyLimit(location->maxBodySize_);
    }
    if (conn->getState() == CONN_READING_BODY)
        conn->processRequestBody();
    if (conn->getState() == CONN_READY_TO_ROUTE)
        routeConnectionRequest(conn, pollfd_pos);
    if (conn->getState() == CONN_WRITING_RESPONSE)
        poll_fds_[pollfd_pos].events = POLLOUT;
}

ConnEventResult Server::handleConnectionWrite(Connection *conn, size_t pollfd_pos)
{
    SendResult status = conn->sendResponse();
    if (status == SEND_FAILED || (status == SEND_COMPLETE && conn->getState() == CONN_CLOSING))
        return CLOSE_CONNECTION;
    if (status == SEND_COMPLETE)
    {
        conn->resetForNewRequest();
        if (!conn->hasBufferedInput())
        {
            poll_fds_[pollfd_pos].events = POLLIN;
            return CONTINUE_CONNECTION;
        }
        processBufferedRequest(conn, pollfd_pos);
        if (conn->getState() == CONN_WAITING_FOR_REQUEST || conn->getState() == CONN_READING_HEADERS || conn->getState() == CONN_READING_BODY)
            poll_fds_[pollfd_pos].events = POLLIN;
        else if (conn->getState() == CONN_READY_TO_ROUTE || conn->getState() == CONN_WRITING_RESPONSE)
            poll_fds_[pollfd_pos].events = POLLOUT;
    }
    return CONTINUE_CONNECTION;
}

ConnEventResult Server::handleConnectionEvent(size_t pollfd_pos)
{
    int fd = poll_fds_[pollfd_pos].fd;
    short revents = poll_fds_[pollfd_pos].revents;
    Connection *conn = getConnection(fd);
    if (!conn || (revents & (POLLERR | POLLNVAL)))
        return CLOSE_CONNECTION;
    if ((revents & POLLHUP))
        conn->closeAfterCurrentResponse();
    SocketReadResult readResult = SOCKET_NOT_READ;
    if (revents & POLLIN)
    {
        readResult = conn->readFromSocket();
        if (readResult == SOCKET_READ_ERROR)
            return CLOSE_CONNECTION;
        if (readResult == SOCKET_DATA_READ && conn->getState() != CONN_RUNNING_CGI)
            processBufferedRequest(conn, pollfd_pos);
        if (readResult == SOCKET_EOF)
            conn->closeAfterCurrentResponse();
    }
    if (revents & POLLOUT)
        return handleConnectionWrite(conn, pollfd_pos);
    if ((revents & POLLHUP) || readResult == SOCKET_EOF)
    {
        if (conn->getState() == CONN_RUNNING_CGI)
        {
            poll_fds_[pollfd_pos].fd = -1;
            poll_fds_[pollfd_pos].events = 0;
            return CONTINUE_CONNECTION;
        }
        if (conn->hasPendingResponse())
        {
            poll_fds_[pollfd_pos].events = POLLOUT;
            return CONTINUE_CONNECTION;
        }
        if (readResult != SOCKET_DATA_READ)
            return CLOSE_CONNECTION;
    }
    return CONTINUE_CONNECTION;
}

void    Server::handleListenerEvent(size_t pollfd_pos, Listener *listener)
{
    short revents = poll_fds_[pollfd_pos].revents;
    if (revents & (POLLERR | POLLHUP | POLLNVAL))
        throw std::runtime_error("Listening socket poll error");
    if (!(revents & POLLIN))
        return;
    std::string clientIp;
    int clientFd = acceptConnection(clientIp, pollfd_pos);
    if (clientFd == -1)
        return;
    if (conns_.size() >= MAX_CONNECTIONS + MAX_OVERLOAD_CONNECTIONS)
    {
        log_.record(Log::WARN, "Server", "Connection hard limit reached; closing fd="
                    + toString(clientFd));
        close(clientFd);
        return;
    }
    Connection *new_client = new Connection(clientFd, clientIp, listener,
                                            listener->configs_[0], log_);
    conns_[clientFd] = new_client;
    if (conns_.size() > MAX_CONNECTIONS)
    {
        log_.record(Log::WARN, "Server", "Connection soft limit reached; sending 503 to fd="
                    + toString(clientFd));
        new_client->queueErrorResponse(503);
        addFdToPoll(clientFd, POLLOUT);
        return;
    }
    addFdToPoll(clientFd, POLLIN);
    log_.record(Log::DEBUG, "Server", "Connected fd=" + toString(clientFd)
                + " remote=" + clientIp);
}

void	Server::processEvents()
{
    std::vector<int>			dead_fds;

    for (size_t i = 0; i < poll_fds_.size(); ++i)
    {
        if (poll_fds_[i].revents == 0)
            continue;
        Connection *cgiOwner = getCgiOwner(poll_fds_[i].fd);
        if (cgiOwner)
        {
            handleCgiEvent(cgiOwner, i);
            continue ;
        }
        Listener *listener = getListener(poll_fds_[i].fd);
        if (listener)
        {
            handleListenerEvent(i, listener);
            continue ;
        }
        if (handleConnectionEvent(i) == CLOSE_CONNECTION)
            dead_fds.push_back(poll_fds_[i].fd);
    }
    cleanDeadFds(dead_fds);
}

void Server::checkClientTimeouts()
{
    std::vector<int> timedOutFds;
    for (std::map<int, Connection *>::iterator it = conns_.begin(); it != conns_.end(); ++it)
    {
        if (it->second->headerDeadlineExpired())
        {
            log_.record(Log::WARN, "Connection", "fd=" + toString(it->first)
                        + " header deadline expired");
            timedOutFds.push_back(it->first);
        }
        else if (it->second->bodyDeadlineExpired())
        {
            log_.record(Log::WARN, "Connection", "fd=" + toString(it->first)
                        + " body deadline expired");
            timedOutFds.push_back(it->first);
        }
        else if (it->second->clientHasTimedOut())
        {
            log_.record(Log::DEBUG, "Connection", "fd=" + toString(it->first)
                        + " idle timeout");
            timedOutFds.push_back(it->first);
        }
    }
    cleanDeadFds(timedOutFds);
}

void Server::runServer()
{
    try
    {
        while (!shutdownRequested_)
        {
            int ready = poll(&poll_fds_[0], poll_fds_.size(), POLL_TIMEOUT_MS);
            if (ready < 0)
                continue ;
            if (ready > 0)
                processEvents();
            checkCgiChildren();
            checkClientTimeouts();
            log_.flush();
        }
    }
    catch (const std::exception &error)
    {
        log_.record(Log::ERROR, "Server", std::string("Fatal runtime error: ") + error.what());
        log_.flush();
        throw;
    }
    log_.record(Log::INFO, "Server", "Server shutdown requested");
}
