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

int init_server_socket(int port)
{
	//socket basic setup
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);//AF_INET = IPv4, SOCK_STREAM = TCP, 0 = default protocol (TCP for SOCK_STREAM)
	if (server_fd < 0)
		return -1;
	if (set_non_blocking(server_fd) < 0)
	{
		close(server_fd);
		return (-1);
	}
	int opt_active = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt_active, sizeof(opt_active)) < 0)//allow quick reuse of port, bypassing TIME_WAIT limitations
	{
		close(server_fd);
		return -1;  
	}
	//define port and address for binding
	sockaddr_in address;
	std::memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;//IPv4
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);//converts to network byte order. Eventually use INADDR_ANY
	address.sin_port = htons(static_cast<unsigned short>(port));//convert port to network byte order
	//bind socket with address/port
	if (bind(server_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0)
	{
		close(server_fd);
		return -1;
	}
	//set socket to listen for incoming connections
	if (listen(server_fd, 10) < 0)//10 = backlog, max number of pending connections
	{
		close(server_fd);
		return -1;
	}
	return server_fd;
}

int handle_client(Connection &client)
{
    // client.fd->revents = 0;//reset revents to 0 - in calling function?
    std::cout << "Handling client connection (fd: " << client._fd << ")\n";
    char buffer[1024];
    ssize_t bytes_received = recv(client._fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received == 0)
    {
        std::cout << "Client disconnected (fd: " << client._fd << ")\n";
        close(client._fd);
        return -1;
    }
    else if (bytes_received < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        std::cerr << "Error receiving data from client (fd: " << client._fd << ")\n";
        close(client._fd);
        return -1;
    }
    else
    {
        buffer[bytes_received] = '\0';
        std::cout << "Received data:\n" << buffer << "\n";
        client.in_buffer.append(buffer, bytes_received);
        send(client._fd, "HTTP/1.1 200 OK\r\n\r\n", 17, 0);
    }
    return 0;
}

void	addClient(std::vector<struct pollfd> &poll_fds,
	std::map<int, Connection> &clients, int client_fd)
{
	struct pollfd	entry;

	std::cout << "Handling client connection (fd: " << client_fd << ")\n";
	Connection new_client(client_fd);
	clients[client_fd] = new_client;
	entry.fd = client_fd;
	entry.events = POLLIN;
	entry.revents = 0;
	poll_fds.push_back(entry);
	std::cout << "Client " << client_fd << ": Connected\n";
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

int main()
{
	std::map<int, Connection>	clients;
	std::vector<struct pollfd>	poll_fds;
	struct pollfd				entry;
	int	port = DEFAULT_PORT;
	int server_fd = init_server_socket(port);
	int	client_fd;

	if (server_fd < 0)
	{
		std::cerr << "Failed to create server socket\n";
		return 1;
	}
	entry.fd = server_fd;
	entry.events = POLLIN;
	entry.revents = 0;
	poll_fds.push_back(entry);
	std::cout << "Server listening on port " << port << "\n";

    signal(SIGPIPE, SIG_IGN);//ignore SIGPIPE to prevent server from crashing when sending to a closed socket
	while (true)
	{
		if (poll(&poll_fds[0], poll_fds.size(), -1) <= 0)
			continue ;

		std::vector<struct pollfd>	tmp = poll_fds;
		std::vector<int>			dead_fds;
		for (size_t i = 0; i < tmp.size(); ++i)
		{
			if (!(tmp[i].revents & POLLIN))
				continue ;
			if (tmp[i].fd == server_fd)
			{
				//poll_fds[0].revents = 0;//reset revents for server socket - only needed if we change for loop to use poll_fds instead of tmp
				client_fd = accept(server_fd, NULL, NULL);
				if (client_fd < 0 || clients.size() >= MAX_CONNECTIONS)
					continue;
				if (set_non_blocking(client_fd) < 0)
				{
        			std::cerr << "Error setting client socket to non-blocking\n";
					close(client_fd);
					continue ;
				}
				addClient(poll_fds, clients, client_fd);
			}
			else if (handle_client(clients[tmp[i].fd]) == -1)
			{
				dead_fds.push_back(tmp[i].fd);
				clients.erase(tmp[i].fd);
			}
		}
		cleanDeadFds(poll_fds, dead_fds);
	}
	for (size_t i = 0; i < poll_fds.size(); ++i)
	{
		close(client_fd);
		clients.erase(client_fd);
	}
	return (0);
}