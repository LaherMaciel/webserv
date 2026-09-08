/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lahermaciel <lahermaciel@student.42.fr>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/24 20:16:12 by lahermaciel       #+#    #+#             */
/*   Updated: 2026/09/08 18:56:52 by lahermaciel      ###   ########.fr       */
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
#include <sstream> // for ostringstream to create the toString

std::string toString(size_t n)
{
    std::ostringstream oss;
    oss << n;
    return (oss.str());
}

std::string statusText(int code)
{
    switch (code)
    {
        case 200: return "200 OK\r\n";
        case 400: return "400 Bad Request\r\n";
        case 404: return "404 Not Found\r\n";
        case 431: return "431 Request Header Fields Too Large\r\n";
        case 500: return "500 Internal Server Error\r\n";
        case 501: return "501 Not Implemented";
        default:  return "";
    }
}

int set_non_blocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);//fcntl stands for file control, F_GETFL gets the file status flags
    if (flags == -1)//if fcntl fails, it returns -1
        return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)//F_SETFL sets the file status flags, O_NONBLOCK makes the socket non-blocking
        return -1;
    return 0;
}

//Moved inner_loop() to server.cpp

//Moved the while loop to member function runServer() in server.cpp
int server_loop()//to rename or just move what's left in here to main()?
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
    // while (true)
    // {
    //     if (poll(&server.poll_fds[0], server.poll_fds.size(), -1) <= 0)
    //         continue ;
    //     inner_loop(server);
    // }
    server.runServer();
    return (0);
}

int main()
{
    return (server_loop());
}
