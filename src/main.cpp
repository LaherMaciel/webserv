/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: belinore <belinore@student.42lisboa.com    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/24 20:16:12 by lahermaciel       #+#    #+#             */
/*   Updated: 2026/09/02 17:34:01 by belinore         ###   ########.fr       */
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

int main()
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
    server.runServer();
    
    return (0);
}
