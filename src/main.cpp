/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lahermaciel <lahermaciel@student.42.fr>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/24 20:16:12 by lahermaciel       #+#    #+#             */
/*   Updated: 2026/08/24 20:16:37 by lahermaciel      ###   ########.fr       */
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

int set_non_blocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);//fcntl stands for file control, F_GETFL gets the file status flags
    if (flags == -1)//if fcntl fails, it returns -1
        return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)//F_SETFL sets the file status flags, O_NONBLOCK makes the socket non-blocking
        return -1;
    return 0;
}

void cleanDeadFds(std::vector<struct pollfd> &poll_fds,
		std::vector<int> &dead_fds)
{
	for (int i = dead_fds.size() -1; i >= 0; --i)
	{
		for (int j = poll_fds.size() - 1; j >= 0; --j)
		{
			if (poll_fds[j].fd == dead_fds[i])
				poll_fds.erase(poll_fds.begin() + j);
		}
	}
}

int server_loop()
{
	Server	server;
	struct pollfd	entry;
	int	port = DEFAULT_PORT;
	int server_fd = server.init_server_socket(port);
	int	client_fd;

	if (server_fd < 0)
	{
		std::cerr << "Failed to create server socket\n";
		return 1;
	}
	entry.fd = server_fd;
	entry.events = POLLIN;
	entry.revents = 0;
	server.poll_fds.push_back(entry);
	std::cout << "Server listening on port " << port << "\n";

    signal(SIGPIPE, SIG_IGN);//ignore SIGPIPE to prevent server from crashing when sending to a closed socket
	while (true)
	{
		if (poll(&server.poll_fds[0], server.poll_fds.size(), -1) <= 0)
			continue ;

		std::vector<struct pollfd>	tmp = server.poll_fds;
		std::vector<int>			dead_fds;
		for (size_t i = 0; i < tmp.size(); ++i)
		{
			if (!(tmp[i].revents & POLLIN))
				continue ;
			if (tmp[i].fd == server_fd)
			{
				//poll_fds[0].revents = 0;//reset revents for server socket - only needed if we change for loop to use poll_fds instead of tmp
				client_fd = accept(server_fd, NULL, NULL);
				if (client_fd < 0 || server.conns.size() >= MAX_CONNECTIONS)
					continue;
				if (set_non_blocking(client_fd) < 0)
				{
        			std::cerr << "Error setting client socket to non-blocking\n";
					close(client_fd);
					continue ;
				}
				server.addClient(client_fd);
			}
			else if (server.conns[tmp[i].fd].handle_client() == -1)
			{
				dead_fds.push_back(tmp[i].fd);
				server.conns.erase(tmp[i].fd);
			}
		}
		cleanDeadFds(server.poll_fds, dead_fds);
	}
	for (size_t i = 0; i < server.poll_fds.size(); ++i)
	{
		close(client_fd);
		server.conns.erase(client_fd);
	}
	return (0);
}

int main()
{
	server_loop();
	return 0;
}
