
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

Listener::Listener(ServerConfig *config)
    : fd_(-1), host_(config->host_), port_(config->port_)
{
    configs_.push_back(config);
}

Server::Server(const std::vector<ServerConfig>& config): config_(config) {}

Server::~Server()
{
    for (std::map<int, Connection *>::iterator it = conns_.begin(); it != conns_.end(); ++it)
        delete it->second;
    for (size_t i = 0; i < closingConnections_.size(); ++i)
        delete closingConnections_[i];
    conns_.clear();
    closingConnections_.clear();
    poll_fds_.clear();
    for (size_t i = 0; i < listeners_.size(); ++i)
    {
        if (listeners_[i].fd_ != -1)
            close(listeners_[i].fd_);
    }
    
}

void Server::addFdToPoll(int fd, short events)//used for both the server socket and the client sockets
{
    struct pollfd entry;
    entry.fd = fd;
    entry.events = events;
    entry.revents = 0;
    poll_fds_.push_back(entry);
}

void	Server::addClient(int clientfd, const std::string &remoteAddress,
                       Listener *listener)
{
    std::cout << "Handling client connection (fd: " << clientfd << ")\n";
    Connection *new_client = new Connection(clientfd, remoteAddress, listener);
    conns_[clientfd] = new_client;
    addFdToPoll(clientfd);
    std::cout << "Client " << clientfd << ": Connected\n";
}

int    Server::acceptConnection(std::string &remoteAddress, size_t pollfd_pos)
{
    sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    std::memset(&clientAddr, 0, sizeof(clientAddr));

    int clientfd = accept(poll_fds_[pollfd_pos].fd, reinterpret_cast<sockaddr*>(&clientAddr), &clientAddrLen);

    if (clientfd < 0 || conns_.size() >= MAX_CONNECTIONS)
    {
        std::cerr << "Error accepting new connection\n";
        if (clientfd >= 0)
            close(clientfd);
        return (-1);
    }
    if (set_non_blocking(clientfd) < 0)
    {
        std::cerr << "Error setting client socket to non-blocking\n";
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

void Server::initSocket(Listener &listener)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);//AF_INET = IPv4, SOCK_STREAM = TCP, 0 = default protocol (TCP for SOCK_STREAM)
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
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt_active, sizeof(opt_active)) < 0)//allow quick reuse of port, bypassing TIME_WAIT limitations
    {
        throw std::runtime_error("Failed to set socket options");
    }
}

// void Server::bindSocket(Listener &listener)
// {
//     //define port and address for binding
//     sockaddr_in address;
//     std::memset(&address, 0, sizeof(address));
//     address.sin_family = AF_INET;//IPv4
//     address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);//converts to network byte order. Eventually use INADDR_ANY
//     address.sin_port = htons(static_cast<unsigned short>(listener.port_));//convert port to network byte order
//     if (bind(listener.fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) //bind socket with address/port
//     {
//         throw std::runtime_error("Failed to bind socket");
//     }
//     if (listen(listener.fd_, MAX_PENDING_CONNECTIONS) < 0)//set socket to listen for incoming connections
//     {
//         throw std::runtime_error("Failed to listen on socket");
//     }
// }

//new approach to binding required to ensure each listener only listens on the configured host ip
//in the old approach we would have had to parse the ip string to an unsigned long
void Server::bindSocket(Listener &listener)
{
    //define port and address for binding
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
    int bindResult = bind(listener.fd_, result->ai_addr, result->ai_addrlen); //bind socket with address/port
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
            std::cout << "Virtual host " << config->serverName_
                      << " added to Listener " << listeners_[i].fd_
                      << ", host: " << config->host_
                      << ", port: " << config->port_ << "\n";
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
        if (!addToExistingListener(config))
        {
            Listener newListener(config);
            initSocket(newListener);
            bindSocket(newListener);
            addFdToPoll(newListener.fd_);
            listeners_.push_back(newListener);
            std::string name = config->serverName_.empty() ? "[not named]" : config->serverName_;
            std::cout << "Server " << name << " added Listener fd "
                      << newListener.fd_ << ", host: " << newListener.host_
                      << ", port: " << newListener.port_ << "\n";
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
        std::string name = host->second;
        for (size_t i = 0; i < listener->configs_.size(); ++i)
        {
            if (listener->configs_[i]->serverName_ == name)//need to compare multiple names
            {
                return (listener->configs_[i]);
            }
        }
    }
    return (listener->configs_[0]);
}

// moved the multiple request in buffer check to main loop, after first response is fully sent
ConnectionStatus Server::processBufferedRequest(Connection *conn, size_t pollfd_pos)
{
    ConnectionStatus status = conn->handleRequest();
    if (status == REQUEST_READY)
    {
        Response response;
        CgiInfo cgiInfo;
        ServerConfig *config = selectServerConfig(conn);
        conn->setSelectedConfig(config);
        Router router(*config);
        RouteType result = router.routeRequest(conn->getRequest(), response, cgiInfo);
        if (result != ROUTE_CGI)
            conn->queueResponse(response);
        else
            return startCgi(conn, cgiInfo, pollfd_pos);
    }
    if (!conn->hasPendingResponse())
        return WAIT_FOR_MORE;
    poll_fds_[pollfd_pos].events = POLLOUT;
    return RESPONSE_READY;
}

ConnectionStatus Server::handleConnectionEvent(size_t pollfd_pos)
{
    int fd = poll_fds_[pollfd_pos].fd;
    short revents = poll_fds_[pollfd_pos].revents;
    Connection *conn = getConnection(fd);
    if (!conn || (revents & (POLLERR | POLLNVAL)))
        return CLOSE_CONNECTION;
    ConnectionStatus status;
    if (revents & POLLIN)
    {
        if (conn->readFromSocket() == -1)
            return CLOSE_CONNECTION;
        else if (conn->cgiIsRunning())
            return CONTINUE_CONNECTION;
        status = processBufferedRequest(conn, pollfd_pos);
        if (status == CLOSE_CONNECTION)
            return CLOSE_CONNECTION;
    }
    if (revents & POLLOUT)
    {
        status = conn->sendResponse();
        if (status == WAIT_FOR_MORE)
            return CONTINUE_CONNECTION;
        if ((status == RESPONSE_SENT && conn->getCloseAfterResponse())
            || status == CLOSE_CONNECTION)
            return CLOSE_CONNECTION;
        else if (status == RESPONSE_SENT)
        {
            conn->resetForNewRequest();
            if (!conn->hasBufferedInput())
            {
                poll_fds_[pollfd_pos].events = POLLIN;
                return CONTINUE_CONNECTION;
            }
            status = processBufferedRequest(conn, pollfd_pos);
            if (status == WAIT_FOR_MORE)
                poll_fds_[pollfd_pos].events = POLLIN;
            else if (status == RESPONSE_READY)
                poll_fds_[pollfd_pos].events = POLLOUT;
        }
    }
    if (revents & POLLHUP)
        return CLOSE_CONNECTION;
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
    if (clientFd != -1)
        addClient(clientFd, clientIp, listener);
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

void Server::runServer()
{
    while (true)
    {
        int ready = poll(&poll_fds_[0], poll_fds_.size(), POLL_TIMEOUT_MS);
        if (ready < 0)
            continue ;
        if (ready > 0)
            processEvents();
        checkCgiChildren();
    }
}
