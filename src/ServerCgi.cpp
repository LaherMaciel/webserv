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
    LocationConfig locationStatic;
    locationStatic.path_ = "/";
    locationStatic.root_ = "./www";
    locationStatic.index_ = "index.html";
    locationStatic.allowedMethods_.push_back("GET");
    config.locations_.push_back(locationStatic);
    LocationConfig locationCgi;
    locationCgi.path_ = "/cgi-bin";
    locationCgi.root_ = "./www/cgi-bin";
    locationCgi.cgiHandlers_[".py"] = "/usr/bin/python3";
    locationCgi.allowedMethods_.push_back("GET");
    locationCgi.allowedMethods_.push_back("POST");
    config.locations_.push_back(locationCgi);
    LocationConfig locationUpload;
    locationUpload.path_ = "/upload";
    locationUpload.root_ = "./www/upload";
    locationUpload.uploadStore_ = "./www/upload";
    locationUpload.allowedMethods_.push_back("POST");
    locationUpload.allowedMethods_.push_back("GET");
    locationUpload.allowedMethods_.push_back("DELETE");
    config.locations_.push_back(locationUpload);
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
    addFdToPoll(conn->getCgiOutputFd(), POLLIN);
    cgiOwners_[conn->getCgiOutputFd()] = conn;
    if (conn->getCgiInputFd() != -1)
    {
        addFdToPoll(conn->getCgiInputFd(), POLLOUT);
        cgiOwners_[conn->getCgiInputFd()] = conn;
    }
    poll_fds_[pollfd_pos].events = POLLIN;
    return CGI_STARTED;
}

void Server::handleCgiEvent(Connection *cgiOwner, size_t pollfd_pos)
{
    pollfd &cgiPollFd = poll_fds_[pollfd_pos];
    int eventFd = cgiPollFd.fd;
    short revents = cgiPollFd.revents;
    if (revents & (POLLERR | POLLNVAL))
    {
        cgiOwner->queueErrorResponse(500, cgiOwner->getRequest().getVersion());
        updatePollEvents(cgiOwner->getFd(), POLLOUT);
        removeCgiFd(eventFd);
        removeCgiFds(cgiOwner);
        cgiOwner->abortCgi();
        return;
    }
    if ((revents & POLLOUT) && eventFd == cgiOwner->getCgiInputFd())
    {
        ConnectionStatus status = cgiOwner->writeToCGIPipe();
        if (status == WAIT_FOR_MORE)
            return;
        removeCgiFd(eventFd);
        if (status == CGI_IO_ERROR)
        {
            removeCgiFds(cgiOwner);
            updatePollEvents(cgiOwner->getFd(), POLLOUT);
            cgiOwner->abortCgi();
        }
        return;
    }
    if ((revents & (POLLIN | POLLHUP)) && eventFd == cgiOwner->getCgiOutputFd())
    {
        ConnectionStatus status = cgiOwner->readFromCGIPipe();
        if (status == RESPONSE_READY || status == CGI_WAITING_FOR_EXIT ||
            status == CGI_IO_ERROR)
        {
            removeCgiFd(eventFd);
            if (status == RESPONSE_READY)
            {
                removeCgiFds(cgiOwner);
                updatePollEvents(cgiOwner->getFd(), POLLOUT);
                cgiOwner->resetCgiProcess();
            }
            else if (status == CGI_IO_ERROR)
            {
                removeCgiFds(cgiOwner);
                updatePollEvents(cgiOwner->getFd(), POLLOUT);
                cgiOwner->abortCgi();
            }
        }
    }
}

void Server::removeCgiFd(int fd)
{
    if (fd == -1)
        return;
    cgiOwners_.erase(fd);
    for (size_t i = 0; i < poll_fds_.size(); ++i)
    {
        if (poll_fds_[i].fd == fd)
        {
            poll_fds_[i].fd = -1;
            poll_fds_[i].events = 0;
            return;
        }
    }
}

void Server::removeCgiFds(Connection *cgiOwner)
{
    removeCgiFd(cgiOwner->getCgiOutputFd());
    removeCgiFd(cgiOwner->getCgiInputFd());
}

/*
Delayed cleanup of CGI processes for where child was still running during initial waitpid()
check after EOF was received from output pipe OR kill signal was sent to child.
This prevents blocking waitpid() calls in the main event loop and ensures cleanup can 
occur even when server is idle (no new events from poll()).
*/
void Server::checkCgiChildren()
{
    for (std::map<int, Connection *>::iterator it = conns_.begin();
         it != conns_.end(); ++it)
    {
        Connection *conn = it->second;
        if (conn->cgiAbortIsPending())
        {
            removeCgiFds(conn);
            conn->abortCgi();
            continue;
        }
        if (conn->cgiCompletionIsPending())
        {
            if (conn->checkCgiChild() == RESPONSE_READY)
            {
                updatePollEvents(conn->getFd(), POLLOUT);
                conn->resetCgiProcess();
                continue;
            }
        }
        if (conn->cgiHasTimedOut())
        {
            removeCgiFds(conn);
            conn->queueErrorResponse(504, conn->getRequest().getVersion(),
                "Gateway Timeout: CGI process timed out", "text/plain");
            updatePollEvents(conn->getFd(), POLLOUT);
            conn->abortCgi();
            continue;
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
