#ifndef WEBSERV_HPP
# define WEBSERV_HPP

#define DEFAULT_PORT 8080
#define MAX_CONNECTIONS 10

#include <iostream>
#include <string>
#include <vector>
#include <map>

//why a std::string and not. char like the previews?
struct Client
{
	int			fd;
	std::string	in_buffer;
};




int	    handle_client(Client &client);
void	addClient(std::vector<struct pollfd> &poll_fds, std::map<int, Client> &clients, int client_fd);
void    cleanDeadFds(std::vector<struct pollfd> &poll_fds, std::vector<int> &dead_fds);
/*
	I understand the fd
	the events, I fill with what? where?
	the revents, well yeah I have to check that i returned
*/
#endif