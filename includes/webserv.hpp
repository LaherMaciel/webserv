#ifndef WEBSERV_HPP
# define WEBSERV_HPP

#define DEFAULT_PORT 8080
#define MAX_CONNECTIONS 10
#define MAX_PENDING_CONNECTIONS 10

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <poll.h>

class Connection;

void    cleanDeadFds(std::vector<struct pollfd> &poll_fds, std::vector<int> &dead_fds);
int 	set_non_blocking(int fd);

#endif