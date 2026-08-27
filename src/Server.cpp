
#include "webserv.hpp"
#include "Server.hpp"
#include "Connection.hpp"
#include <map>
#include <cstring>//for memset
#include <sys/socket.h>//for socket(), bind(), listen(), accept()
#include <netinet/in.h>//for sockaddr_in
#include <unistd.h>//for close
#include <iostream>
#include <poll.h>//for poll()

Server::Server(){}

Server::~Server() {}

Server::Server(const Server& other)
{
    if (this != &other)
    {
        conns = other.conns;
        poll_fds = other.poll_fds;
    }
}

Server& Server::operator=(const Server& other)
{
    if (this != &other)
    {
        conns = other.conns;
        poll_fds = other.poll_fds;
    }
    return *this;
}

void	Server::addClient(int client_fd)
{
	struct pollfd	entry;

	std::cout << "Handling client connection (fd: " << client_fd << ")\n";
	Connection new_client(client_fd);
	conns[client_fd] = new_client;
	entry.fd = client_fd;
	entry.events = POLLIN;
	entry.revents = 0;
	poll_fds.push_back(entry);
	std::cout << "Client " << client_fd << ": Connected\n";
}

int Server::init_server_socket(int port)
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
