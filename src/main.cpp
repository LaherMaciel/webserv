/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: belinore <belinore@student.42lisboa.com    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/24 20:16:12 by lahermaciel       #+#    #+#             */
/*   Updated: 2026/08/28 17:24:42 by belinore         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "webserv.hpp"
#include "Server.hpp"
#include "Connection.hpp"
#include <iostream>
#include <cstring>//for memset
#include <sys/socket.h>//for socket(), bind(), listen(), accept()
#include <netinet/in.h>//for sockaddr_in
#include <unistd.h>//for close
#include <poll.h>//for poll()
#include <csignal>//for signal()
#include <fcntl.h>//for fcntl()
#include <cerrno>//for errno
#include <map> // well, to add map
#include <vector>
#include <stdexcept> // to use std::runtime_error

int set_non_blocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);//fcntl stands for file control, F_GETFL gets the file status flags
    if (flags == -1)//if fcntl fails, it returns -1
        return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)//F_SETFL sets the file status flags, O_NONBLOCK makes the socket non-blocking
        return -1;
    return 0;
}

static void	inner_loop(Server &server)
{
    int     client_fd;
    std::vector<int>			dead_fds;

    for (size_t i = 0; i < server.poll_fds.size(); ++i)
    {
        if (!(server.poll_fds[i].revents & POLLIN))
            continue ;
        if (server.poll_fds[i].fd == server.fd)
        {
            client_fd = server.acceptConnection();
            if (client_fd == -1)
                continue ;
            server.addClient(client_fd);
        }
        else if (server.conns[server.poll_fds[i].fd]->handle_client() == -1)
        {
            dead_fds.push_back(server.poll_fds[i].fd);
        }
    }
    server.cleanDeadFds(dead_fds);
}

int server_loop()
{
    Server  server;

    try
    {
        server.startServer();
    }
    catch (std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        std::cerr << "Failed to create server socket\n";
        return (-1);
    }
    signal(SIGPIPE, SIG_IGN);//ignore SIGPIPE to prevent server from crashing when sending to a closed socket
    while (true)
    {
        if (poll(&server.poll_fds[0], server.poll_fds.size(), -1) <= 0)
            continue ;
        inner_loop(server);
    }
    return (0);
}

int main()
{
    return (server_loop());
}
