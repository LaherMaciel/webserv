#include "webserv.hpp"
#include "Server.hpp"
#include "Response.hpp"
#include <iostream>

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

void Server::startCgi(Connection *conn, const CgiInfo &cgiInfo, size_t pollfd_pos)
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
        return;
    }
    addFdToPoll(conn->getCgiOutputFd(), POLLIN);
    cgiOwners_[conn->getCgiOutputFd()] = conn;
    if (conn->getCgiInputFd() != -1)
    {
        addFdToPoll(conn->getCgiInputFd(), POLLOUT);
        cgiOwners_[conn->getCgiInputFd()] = conn;
    }
    poll_fds_[pollfd_pos].events = POLLIN;
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
        CgiWriteStatus status = cgiOwner->writeToCGIPipe();
        if (status == CGI_WRITING)
            return;
        removeCgiFd(eventFd);
        if (status == CGI_WRITE_ERROR)
        {
            removeCgiFds(cgiOwner);
            updatePollEvents(cgiOwner->getFd(), POLLOUT);
            cgiOwner->abortCgi();
        }
        return;
    }
    if ((revents & (POLLIN | POLLHUP)) && eventFd == cgiOwner->getCgiOutputFd())
    {
        CgiReadStatus status = cgiOwner->readFromCGIPipe();
        if (status == CGI_READING)
            return;
        removeCgiFd(eventFd);
        if (status == CGI_READ_ERROR || status == CGI_OUTPUT_TOO_LARGE)
        {
            removeCgiFds(cgiOwner);
            updatePollEvents(cgiOwner->getFd(), POLLOUT);
            cgiOwner->abortCgi();
        }
        if (status == CGI_OUTPUT_COMPLETE && cgiOwner->checkCgiChild())
        {
            removeCgiFds(cgiOwner);
            updatePollEvents(cgiOwner->getFd(), POLLOUT);
            cgiOwner->resetCgiProcess();
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
            if (conn->checkCgiChild())
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
