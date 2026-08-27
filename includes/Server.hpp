#ifndef SERVER_HPP
# define SERVER_HPP

#include <map>
#include <vector>

class Connection;

class Server
{
	public:
		std::map<int, Connection>	conns;
		std::vector<struct pollfd>	poll_fds;

		Server();
		~Server();
		Server(const Server& other);
		Server& operator=(const Server& other);
		void	addClient(int client_fd);
        int     init_server_socket(int port);
};

#endif