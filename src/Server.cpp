
#include "webserv.hpp"
#include "Server.hpp"
#include "Connection.hpp"
#include "Response.hpp"
#include "Router.hpp"
#include <map>
#include <cstring>//for memset
#include <sys/socket.h>//for socket(), bind(), listen(), accept()
#include <netinet/in.h>//for sockaddr_in
#include <unistd.h>//for close
#include <iostream>
#include <poll.h>//for poll()
#include <stdexcept>//for exception types

static const int POLL_TIMEOUT_MS = 100;

//temp function
ServerConfig setServerConfig()
{
    ServerConfig config;
    config.port_ = DEFAULT_PORT;
    config.host_ = "localhost";
    config.serverName_ = "DefaultServer";
    config.maxBodySize_ = 1000000; // 1 MB
    LocationConfig location;
    location.path_ = "/";
    location.root_ = "./www";
    location.index_ = "index.html";
    config.locations_.push_back(location);
    config.locations_[0].allowedMethods_.push_back("GET");
    location.path_ = "/cgi-bin";
    location.root_ = ".";
    location.cgiHandlers_[".py"] = "/Library/Frameworks/Python.framework/Versions/3.9/bin/python3";
    config.locations_.push_back(location);
    config.locations_[1].allowedMethods_.push_back("GET");
    return config;
}

Server::Server(): config_(setServerConfig()), fd_(-1), port_(DEFAULT_PORT), router_(config_) {}

Server::Server(const ServerConfig& config): config_(config), fd_(-1), port_(config.port_), router_(config_) {}

Server::~Server()
{
    for (std::map<int, Connection *>::iterator it = conns_.begin(); it != conns_.end(); ++it)
        delete it->second;
    for (size_t i = 0; i < closingConnections_.size(); ++i)
        delete closingConnections_[i];
    conns_.clear();
    closingConnections_.clear();
    poll_fds_.clear();
    if (fd_ != -1)
        close(fd_);
}

void Server::addFdToPoll(int fd)//used for both the server socket and the client sockets
{
    struct pollfd entry;
    entry.fd = fd;
    entry.events = POLLIN;
    entry.revents = 0;
    poll_fds_.push_back(entry);
}

void	Server::addClient(int clientfd)
{
    std::cout << "Handling client connection (fd: " << clientfd << ")\n";
    Connection *new_client = new Connection(clientfd);
    conns_[clientfd] = new_client;
    addFdToPoll(clientfd);
    std::cout << "Client " << clientfd << ": Connected\n";
}


int    Server::acceptConnection()
{
    int clientfd = accept(fd_, NULL, NULL);

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
    return (clientfd);
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
            int cgiFd = conn->getCgiOutputFd();
            if (cgiFd != -1)
            {
                cgiOwners_.erase(cgiFd);
                for (size_t j = 0; j < poll_fds_.size(); ++j)
                {
                    if (poll_fds_[j].fd == cgiFd)
                    {
                        poll_fds_[j].fd = -1;
                        poll_fds_[j].events = 0;
                        break;
                    }
                }
            }
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

void Server::initSocket()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);//AF_INET = IPv4, SOCK_STREAM = TCP, 0 = default protocol (TCP for SOCK_STREAM)
    if (fd < 0)
    {
        throw std::runtime_error("Failed to create socket");
    }
    fd_ = fd;
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

void Server::bindSocket()
{
    //define port and address for binding
    sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;//IPv4
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);//converts to network byte order. Eventually use INADDR_ANY
    address.sin_port = htons(static_cast<unsigned short>(port_));//convert port to network byte order
    if (bind(fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) //bind socket with address/port
    {
        throw std::runtime_error("Failed to bind socket");
    }
    if (listen(fd_, MAX_PENDING_CONNECTIONS) < 0)//set socket to listen for incoming connections
    {
        throw std::runtime_error("Failed to listen on socket");
    }
}

void Server::startServer()
{
    initSocket();
    bindSocket();
    addFdToPoll(fd_);
    std::cout << "Server listening on port " << port_ << "\n";
}

Connection *Server::getConnection(int fd)
{
    std::map<int, Connection *>::iterator it = conns_.find(fd);
    if (it == conns_.end())
        return NULL;
    return it->second;
}

Connection *Server::getCgiOwner(int fd)
{
    std::map<int, Connection *>::iterator it = cgiOwners_.find(fd);
    if (it == cgiOwners_.end())
        return NULL;
    return it->second;
}

ConnectionStatus Server::startCgi(Connection *conn, const CgiInfo &cgiInfo, Response &response, int pollfd_pos)
{
    try
    {
        conn->startCgi(cgiInfo);
    }
    catch(const std::exception& e)
    {
        std::cerr << "Error starting CGI: " << e.what() << std::endl;
        response.setBody("Internal Server Error: Failed to start CGI", "text/plain");
        response.setStatusCode(500);
        conn->queueResponse(response);
        poll_fds_[pollfd_pos].events = POLLOUT;
        conn->abortCgi();
        return RESPONSE_READY;
    }
    int cgiFd = conn->getCgiOutputFd();
    addFdToPoll(cgiFd);
    cgiOwners_[cgiFd] = conn;
    poll_fds_[pollfd_pos].events = 0;
    return CGI_STARTED;
}

ConnectionStatus Server::handleConnection(int fd, int pollfd_pos)
{
    Connection *conn = getConnection(fd);//safer than using conns_[fd] directly
    if (!conn)
        return CLOSE_CONNECTION;
    ConnectionStatus status = conn->handleRequest();
    if (status == CLOSE_CONNECTION || status == WAIT_FOR_MORE)
        return status;
    if (status == REQUEST_READY)
    {
        Response response;
        CgiInfo cgiInfo;
        RouteType result = router_.routeRequest(conn->getRequest(), response, cgiInfo);
        if (result != ROUTE_CGI)
            conn->queueResponse(response);
        else
            return startCgi(conn, cgiInfo, response, pollfd_pos);
    }
    poll_fds_[pollfd_pos].events = POLLOUT;
    return RESPONSE_READY;
}

void    Server::updatePollEvents(int fd, short events)
{
    for (size_t i = 0; i < poll_fds_.size(); ++i)
    {
        if (poll_fds_[i].fd == fd)
        {
            poll_fds_[i].events = events;
            return;
        }
    }
}

void Server::checkCgiChildren()
{
    for (std::map<int, Connection *>::iterator it = conns_.begin();
         it != conns_.end(); ++it)
    {
        Connection *conn = it->second;
        if (conn->isCgiAbortPending())
        {
            conn->abortCgi();
            continue;
        }
        if (!conn->isWaitingForCgiExit())
            continue;
        if (conn->checkCgiChild() == RESPONSE_READY)
        {
            updatePollEvents(conn->getFd(), POLLOUT);
            conn->resetCgiProcess();
        }
    }
    for (size_t i = closingConnections_.size(); i > 0; --i)
    {
        Connection *conn = closingConnections_[i - 1];
        if (conn->abortCgi() == CGI_CLEANUP_DONE)
        {
            delete conn;
            closingConnections_.erase(closingConnections_.begin() + (i - 1));
        }
    }
}

void    Server::handleCgiEvent(Connection *cgiOwner, int pollfd_pos)
{
    int cgiFd = poll_fds_[pollfd_pos].fd;
    short revents = poll_fds_[pollfd_pos].revents;
    if (revents & (POLLERR | POLLNVAL))
    {
        cgiOwner->queueErrorResponse(500, cgiOwner->getRequest().getVersion());
        updatePollEvents(cgiOwner->getFd(), POLLOUT);
        cgiOwners_.erase(cgiFd);
        poll_fds_[pollfd_pos].fd = -1;
        poll_fds_[pollfd_pos].events = 0;
        cgiOwner->abortCgi();
    }
    else if (revents & (POLLIN | POLLHUP))
    {
        ConnectionStatus status = cgiOwner->readFromCGIPipe();
        if (status == RESPONSE_READY || status == CGI_WAITING_FOR_EXIT ||
            status == CGI_IO_ERROR)
        {
            cgiOwners_.erase(cgiFd);
            poll_fds_[pollfd_pos].fd = -1;
            poll_fds_[pollfd_pos].events = 0;
            if (status == RESPONSE_READY)
            {
                updatePollEvents(cgiOwner->getFd(), POLLOUT);
                cgiOwner->resetCgiProcess();
            }
            else if (status == CGI_IO_ERROR)
            {
                updatePollEvents(cgiOwner->getFd(), POLLOUT);
                cgiOwner->abortCgi();
            }
        }
    }
}

void	Server::processEvents()
{
    int     client_fd;
    std::vector<int>			dead_fds;

    for (size_t i = 0; i < poll_fds_.size(); ++i)
    {
        Connection *cgiOwner = getCgiOwner(poll_fds_[i].fd);
        if (cgiOwner)
        {
            handleCgiEvent(cgiOwner, i);
            continue ;
        }
        if (poll_fds_[i].revents & (POLLERR | POLLHUP | POLLNVAL))//if error, hangup, or invalid request, mark fd for removal
        {
            dead_fds.push_back(poll_fds_[i].fd);
            continue ;
        }
        if (poll_fds_[i].revents & POLLIN)
        {
           if (poll_fds_[i].fd == fd_)
           {
                client_fd = acceptConnection();
                if (client_fd == -1)
                    continue ;
                addClient(client_fd);
                continue ;
           }
           ConnectionStatus status = handleConnection(poll_fds_[i].fd, i);
           if (status == CLOSE_CONNECTION)
                dead_fds.push_back(poll_fds_[i].fd);
        }
        if (poll_fds_[i].revents & POLLOUT)
        {
            Connection *conn = getConnection(poll_fds_[i].fd);//safer than using conns_[fd] directly
            if (!conn || conn->sendResponse() == CLOSE_CONNECTION)
                dead_fds.push_back(poll_fds_[i].fd);
        } 
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
