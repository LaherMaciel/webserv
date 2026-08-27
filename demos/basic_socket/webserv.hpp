#ifndef WEBSERV_HPP
# define WEBSERV_HPP

#define DEFAULT_PORT 8080

#include <iostream>
#include <string>

//why a std::string and not. char like the previews?
struct Connection
{
	int			fd;
	std::string	in_buffer;
};


int handle_client(Connection &client);
void	addClient(std::vector<struct pollfd> &poll_fds,
	std::map<int, Connection> &clients, int client_fd);
void cleanDeadFds(std::vector<struct pollfd> &poll_fds,
		std::vector<int> &dead_fds);
/*
	I understand the fd
	the events, I fill with what? where?
	the revents, well yeah I have to check that i returned
*/
#endif