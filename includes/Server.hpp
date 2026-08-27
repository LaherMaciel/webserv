#ifndef SERVER_HPP
# define SERVER_HPP

# include <map>
# include <vector>
# include <exception>

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
        void    addClient(int client_fd);
        int     startServer();
};

#endif