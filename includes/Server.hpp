#ifndef SERVER_HPP
# define SERVER_HPP

# include <map>
# include <vector>
# include <exception>
# include "Connection.hpp"
# include "Router.hpp"
# include "ServerConfig.hpp"

class Server
{
    public:
        Server();
        Server(const ServerConfig& config);
        ~Server();
        void    initSocket();
        void    bindSocket();
        void    addFdToPoll(int fd, short events = POLLIN);
        void    addClient(int client_fd);
        void    startServer();
        ConnectionStatus handleConnection(int fd, size_t pollfd_pos);
        void    processEvents();
        void    runServer();
        int     acceptConnection();
        void    cleanDeadFds(std::vector<int> &deadfds);
        void    updatePollEvents(int fd, short events);
        void    handleCgiEvent(Connection *cgiOwner, size_t pollfd_pos);
        void    checkCgiChildren();
        ConnectionStatus startCgi(Connection *conn, const CgiInfo &cgiInfo, size_t pollfd_pos);

    private:
        ServerConfig config_;
        std::map<int, Connection *>	conns_;
        std::map<int, Connection *> cgiOwners_;
        std::vector<Connection *>   closingConnections_;
        std::vector<struct pollfd>	poll_fds_;
        int     fd_;
        int     port_;
        Router  router_;

        Server(const Server& other);
        Server& operator=(const Server& other);

        Connection *getConnection(int fd);
        Connection *getCgiOwner(int fd);
};

#endif
