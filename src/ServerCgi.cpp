#include "webserv.hpp"
#include "Server.hpp"
#include "Response.hpp"
#include <iostream>

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

Connection *Server::getCgiOwner(int fd)
{
    std::map<int, Connection *>::iterator it = cgiOwners_.find(fd);
    if (it == cgiOwners_.end())
        return NULL;
    return it->second;
}

ConnectionStatus Server::startCgi(Connection *conn, const CgiInfo &cgiInfo, size_t pollfd_pos)
{
    try
    {
        conn->startCgi(cgiInfo);
    }
    catch(const std::exception& e)
    {
        std::cerr << "Error starting CGI: " << e.what() << std::endl;
        conn->queueErrorResponse(500, conn->getRequest().getVersion(),
            "Internal Server Error: Failed to start CGI", "text/plain");
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

void Server::handleCgiEvent(Connection *cgiOwner, size_t pollfd_pos)
{
    pollfd &cgiPollFd = poll_fds_[pollfd_pos];
    short revents = cgiPollFd.revents;
    if (revents & (POLLERR | POLLNVAL))
    {
        cgiOwner->queueErrorResponse(500, cgiOwner->getRequest().getVersion());
        updatePollEvents(cgiOwner->getFd(), POLLOUT);
        cgiOwners_.erase(cgiPollFd.fd);
        cgiPollFd.fd = -1;
        cgiPollFd.events = 0;
        cgiOwner->abortCgi();
    }
    else if (revents & (POLLIN | POLLHUP))
    {
        ConnectionStatus status = cgiOwner->readFromCGIPipe();
        if (status == RESPONSE_READY || status == CGI_WAITING_FOR_EXIT ||
            status == CGI_IO_ERROR)
        {
            cgiOwners_.erase(cgiPollFd.fd);
            cgiPollFd.fd = -1;
            cgiPollFd.events = 0;
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
