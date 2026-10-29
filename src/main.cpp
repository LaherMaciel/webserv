/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lahermaciel <lahermaciel@student.42.fr>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/24 20:16:12 by lahermaciel       #+#    #+#             */
/*   Updated: 2026/10/19 15:38:28 by lahermaciel      ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "webserv.hpp"
#include "Server.hpp"
#include "Connection.hpp"
#include "ServerConfig.hpp"
#include <iostream>
#include <cstring>//for memset
#include <sys/socket.h>//for socket(), bind(), listen(), accept()
#include <netinet/in.h>//for sockaddr_in
#include <unistd.h>//for close
#include <poll.h>//for poll()
#include <csignal>//for signal()
#include <fcntl.h>//for fcntl()
#include <map> // well, to add map
#include <vector>
#include <stdexcept> // to use std::runtime_error

int main(int argc, char **argv)
{
    std::vector<ServerConfig>   configs;
    std::string                 path = DEFAULT_CONFIG;

    if (argc > 2)
    {
        std::cerr << "usage: ./webserv [configuration file]" << std::endl;
        return (1);
    }
    if (argc == 2)
        path = argv[1];
    try
    {
        configs = parseConfig(path);
    }
    catch (std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        return (1);
    }
    printConfig(configs);

    Server  server(configs);
    try
    {
        signal(SIGPIPE, SIG_IGN);//ignore SIGPIPE to prevent server from crashing when sending to a closed socket
        signal(SIGINT, Server::requestShutdown);
        signal(SIGTERM, Server::requestShutdown);
        server.startServer();
        server.runServer();
    }
    catch (std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        std::cerr << "Server stopped due to an error\n";
        return (-1);
    }
    
    return (0);
}
