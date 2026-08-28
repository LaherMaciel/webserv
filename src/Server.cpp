
#include "webserv.hpp"
#include "Server.hpp"
#include "Connection.hpp"
#include <map>
#include <cstring>//for memset
#include <sys/socket.h>//for socket(), bind(), listen(), accept()
#include <netinet/in.h>//for sockaddr_in
#include <unistd.h>//for close
#include <iostream>
#include <poll.h>//for poll()

Server::Server(): fd(-1), port(DEFAULT_PORT){}

Server::~Server()
{
    for (std::map<int, Connection *>::iterator it = conns.begin(); it != conns.end(); ++it)
        delete it->second;
    conns.clear();
    poll_fds.clear();
    close(fd);
}

void	Server::addClient(int clientfd)
{
    struct pollfd	entry;

    std::cout << "Handling client connection (fd: " << clientfd << ")\n";
    Connection *new_client = new Connection(clientfd);
    conns[clientfd] = new_client;
    entry.fd = clientfd;
    entry.events = POLLIN;
    entry.revents = 0;
    poll_fds.push_back(entry);
    std::cout << "Client " << clientfd << ": Connected\n";
}


int    Server::acceptConnection()
{
    int clientfd = accept(fd, NULL, NULL);

    if (clientfd < 0 || conns.size() >= MAX_CONNECTIONS)
    {
        std::cerr << "Error accepting new connection\n";
        if (clientfd >= 0)
            close(clientfd);
        return (-1);
    }
    if (set_non_blocking(clientfd) < 0)
    {
        std::cerr << "Error setting client socket to non-blocking\n";
        close(clientfd);
        return (-1);
    }
    return (clientfd);
}


void Server::cleanDeadFds(std::vector<int> &deadfds)
{
    for (int i = deadfds.size() -1; i >= 0; --i)
    {
        std::map<int, Connection *>::iterator it = conns.find(deadfds[i]);
        if (it != conns.end())
        {
            delete it->second;
            conns.erase(it);
        }
        for (int j = poll_fds.size() - 1; j >= 0; --j)
        {
            if (poll_fds[j].fd == deadfds[i])
                poll_fds.erase(poll_fds.begin() + j);
        }
    }
}

static int init_socket()
{
    //socket basic setup
    int fd = socket(AF_INET, SOCK_STREAM, 0);//AF_INET = IPv4, SOCK_STREAM = TCP, 0 = default protocol (TCP for SOCK_STREAM)
    if (fd < 0)
    {
        close(fd);
        throw ;
    }
    if (set_non_blocking(fd) < 0)
    {
        close(fd);
        throw ;
    }
    int opt_active = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt_active, sizeof(opt_active)) < 0)//allow quick reuse of port, bypassing TIME_WAIT limitations
    {
        close(fd);
        throw ;
    }
    return (fd);
}

static int bind_socket(int port, int fd)
{
    //define port and address for binding
    sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;//IPv4
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);//converts to network byte order. Eventually use INADDR_ANY
    address.sin_port = htons(static_cast<unsigned short>(port));//convert port to network byte order
    //bind socket with address/port
    if (bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0)
    {
        close(fd);
        throw ;
    }
    //set socket to listen for incoming connections
    if (listen(fd, 10) < 0)//10 = backlog, max number of pending connections
    {
        close(fd);
        throw ;
    }
    return (fd);
}

void Server::startServer()
{
    struct pollfd	entry;
    fd = init_socket();
    fd = bind_socket(port, fd);
    entry.fd = fd;
    entry.events = POLLIN;
    entry.revents = 0;
    poll_fds.push_back(entry);
    std::cout << "Server listening on port " << port << "\n";
}
