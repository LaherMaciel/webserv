#ifndef SERVER_HPP
# define SERVER_HPP

# include <map>
# include <vector>
# include <exception>
# include <poll.h>//for poll()

class Connection;

class Server
{
    private: //prohibits copy construct or copy assign, so we don't need to create functions
        std::map<int, Connection *>	conns;
        std::vector<struct pollfd>	poll_fds;
        int     fd;
        int     port;

        Server(const Server& other);
        Server& operator=(const Server& other);

    public:
        Server();
        Server(int port);
        ~Server();
        void    initSocket();
        void    bindSocket();
        void    addFdToPoll(int fd);
        void    addClient(int client_fd);
        void    startServer();
        void    inner_loop();
        void    runServer();
        int     acceptConnection();
        void    cleanDeadFds(std::vector<int> &deadfds);
        void    clearBuffers();
};

#endif