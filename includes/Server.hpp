#ifndef SERVER_HPP
# define SERVER_HPP

# include <map>
# include <vector>
# include <exception>

class Connection;

class Server
{
    public:
        std::map<int, Connection *>	conns;
        std::vector<struct pollfd>	poll_fds;
        int     fd;
        int     port;

        Server();
        ~Server();

        void    addClient(int client_fd);
        void    startServer();
        int     acceptConnection();
        void    cleanDeadFds(std::vector<int> &deadfds);
    private: //prohibits copy construct or copy assign, so we don't need to create functions
        Server(const Server& other);
        Server& operator=(const Server& other);
};

#endif