#ifndef SERVER_HPP
# define SERVER_HPP

# include <map>
# include <vector>
# include <exception>
# include "Connection.hpp"

class Server
{
    public:
        Server();
        Server(int port);
        ~Server();
        void    initSocket();
        void    bindSocket();
        void    addFdToPoll(int fd);
        void    addClient(int client_fd);
        void    startServer();
        ConnectionStatus handleConnection(int fd);
        int     routeRequest(Connection *conn);
        void    processEvents();
        void    runServer();
        int     acceptConnection();
        void    cleanDeadFds(std::vector<int> &deadfds);

    private:
        std::map<int, Connection *>	conns_;
        std::vector<struct pollfd>	poll_fds_;
        int     fd_;
        int     port_;
        Server(const Server& other);
        Server& operator=(const Server& other);
};

#endif