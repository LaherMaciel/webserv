#include "webserv.hpp"
#include <iostream>
#include <cstring>//for memset
#include <sys/socket.h>//for socket(), bind(), listen(), accept()
#include <netinet/in.h>//for sockaddr_in
#include <unistd.h>//for close
#include <poll.h>//for poll()
#include <csignal>//for signal()
#include <fcntl.h>//for fcntl()
#include <cerrno>//for errno
/*
socket()
   ↓
bind()
   ↓
listen()
   ↓
while (1)
{
    select()
       ↓
    accept() OR recv()
       ↓
    extract_message()
       ↓
    send()
}
*/

int init_server_socket(int port)
{
    //socket basic setup
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);//AF_INET = IPv4, SOCK_STREAM = TCP, 0 = default protocol (TCP for SOCK_STREAM)
    if (server_fd < 0)
        return -1;
    int opt_active = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt_active, sizeof(opt_active)) < 0)//allow quick reuse of port, bypassing TIME_WAIT limitations
    {
        close(server_fd);
        return -1;  
    }
    //define port and address for binding
    sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;//IPv4
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);//converts to network byte order. Eventually use INADDR_ANY
    address.sin_port = htons(static_cast<unsigned short>(port));//convert port to network byte order
    //bind socket with address/port
    if (bind(server_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0)
    {
        close(server_fd);
        return -1;
    }
    //set socket to listen for incoming connections
    if (listen(server_fd, 10) < 0)//10 = backlog, max number of pending connections
    {
        close(server_fd);
        return -1;
    }
    return server_fd;
}

//to be implemented once Client class has state
// int send_response(int client_fd, const std::string &response)
// {
//     ssize_t bytes_sent = send(client_fd, response.c_str(), response.size(), 0);
//     if (bytes_sent < 0)
//     {
//         if (errno == EAGAIN || errno == EWOULDBLOCK)
//             return 1; // Non-blocking socket, try again later
//         std::cerr << "Error sending response to client (fd: " << client_fd << ")\n";
//         close(client_fd);
//         return -1;
//     }
//     return 0;
// }

void handle_client(struct pollfd *readyfd)
{
    readyfd->revents = 0;
    std::cout << "Handling client connection (fd: " << readyfd->fd << ")\n";
    char buffer[1024];
    ssize_t bytes_received = recv(readyfd->fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received == 0)
    {
        std::cout << "Client disconnected (fd: " << readyfd->fd << ")\n";
        close(readyfd->fd);
        readyfd->fd = -1;
        readyfd->events = 0;
    }
    else if (bytes_received < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        std::cerr << "Error receiving data from client (fd: " << readyfd->fd << ")\n";
        close(readyfd->fd);
        readyfd->fd = -1;
        readyfd->events = 0;
    }
    else
    {
        buffer[bytes_received] = '\0';
        std::cout << "Received data:\n" << buffer << "\n";
        send(readyfd->fd, "HTTP/1.1 200 OK\r\n\r\n", 17, 0);
    }
}

int get_ready_fd(struct pollfd *poll_fds, int num_fds)
{
    // Check which file descriptor is ready for reading
    for (int i = 0; i < num_fds; i++)
    {
        if (poll_fds[i].revents & POLLIN)
            return i;
    }
    return -1;
}

int set_non_blocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);//fcntl stands for file control, F_GETFL gets the file status flags
    if (flags == -1)//if fcntl fails, it returns -1
        return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)//F_SETFL sets the file status flags, O_NONBLOCK makes the socket non-blocking
        return -1;
    return 0;
}

void handle_new_connection(int server_fd, struct pollfd *poll_fds, int &size)
{
    poll_fds[0].revents = 0;//reset revents for server socket
    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0 || size >= MAX_CONNECTIONS + 1)
    {
        std::cerr << "Error accepting new connection\n";
        if (client_fd >= 0)
            close(client_fd);
        return;
    }
    if (set_non_blocking(client_fd) < 0)
    {
        std::cerr << "Error setting client socket to non-blocking\n";
        close(client_fd);
        return;
    }
    poll_fds[size].fd = client_fd;
    poll_fds[size].events = POLLIN;
    std::cout << "New client connected (fd: " << client_fd << ")\n";
    size++;
}

int main()
{
    int port = DEFAULT_PORT;
    int server_fd = init_server_socket(port);
    if (server_fd < 0)
    {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }
    if (set_non_blocking(server_fd) < 0)
    {
        close(server_fd);
        std::cerr << "Error setting server socket to non-blocking\n";
        return 1;
    }
    std::cout << "Server listening on port " << port << "\n";
    //setup non-blocking
    struct pollfd poll_fds[MAX_CONNECTIONS + 1];
    std::memset(&poll_fds, 0, sizeof(poll_fds));
    poll_fds[0].fd = server_fd;//monitors server socket for incoming connections
    poll_fds[0].events = POLLIN;
    // struct Client clients[MAX_CONNECTIONS];
    // std::memset(&clients, 0, sizeof(clients));
    // for (int i = 0; i < MAX_CONNECTIONS; i++)
    // {
    //     clients[i].fd = -1;
    //     clients[i].index = i;
    // }
    int size = 1;
    signal(SIGPIPE, SIG_IGN);//ignore SIGPIPE to prevent server from crashing when sending to a closed socket
    while (true)
    {
        int ret = poll(&poll_fds[0], size, 100);
        if (ret > 0)
        {
            while (true)
            {
                int ready = get_ready_fd(poll_fds, size);
                if (ready == -1)
                    break;
                if (ready == 0)
                    handle_new_connection(server_fd, poll_fds, size);
                else
                    handle_client(&poll_fds[ready]);
            }
        }
    }
    close(server_fd);
}



