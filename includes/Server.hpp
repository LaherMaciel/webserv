#ifndef SERVER_HPP
# define SERVER_HPP

# include <map>
# include <vector>
# include <exception>
# include "Connection.hpp"
# include "Router.hpp"
# include "ServerConfig.hpp"

struct Listener
{
    int         fd_;
    std::string host_;
    size_t      port_;
    std::vector<ServerConfig *> configs_;
    size_t      maxBodySizeLimit_;
    Listener(ServerConfig *config);
};

class Server
{
    public:
        Server(const std::vector<ServerConfig>& config);
        ~Server();
        void    initSocket(Listener &listener);
        void    bindSocket(Listener &listener);
        void    addFdToPoll(int fd, short events = POLLIN);
        void    addClient(int clientFd, const std::string &remoteAddress,
                          Listener *listener);
        void    startServer();
        ConnectionStatus processBufferedRequest(Connection *conn, size_t pollfd_pos);
        void    processEvents();
        void    runServer();
        int     acceptConnection(std::string &remoteAddress, size_t pollfd_pos);
        void    cleanDeadFds(std::vector<int> &deadfds);
        void    updatePollEvents(int fd, short events);
        void    handleCgiEvent(Connection *cgiOwner, size_t pollfd_pos);
        void    handleListenerEvent(size_t pollfd_pos, Listener *listener);
        ConnectionStatus handleConnectionEvent(size_t pollfd_pos);
        void    removeCgiFd(int fd);
        void    removeCgiFds(Connection *cgiOwner);
        void    checkCgiChildren();
        ConnectionStatus startCgi(Connection *conn, const CgiInfo &cgiInfo, size_t pollfd_pos);
        bool    addToExistingListener(ServerConfig *config);
        ServerConfig *selectServerConfig(Connection *connection);

    private:
        std::vector<ServerConfig> config_;
        std::vector<Listener> listeners_;
        std::map<int, Connection *>	conns_;
        std::map<int, Connection *> cgiOwners_;
        std::vector<Connection *>   closingConnections_;
        std::vector<struct pollfd>	poll_fds_;

        Server(const Server& other);
        Server& operator=(const Server& other);

        Connection *getConnection(int fd);
        Connection *getCgiOwner(int fd);
        Listener   *getListener(int fd);
};

#endif
