#include "webserv.hpp"
#include <iostream>
#include <cstring>//for memset
#include <sys/socket.h>//for socket(), bind(), listen(), accept()
#include <netinet/in.h>//for sockaddr_in
#include <unistd.h>//for close
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
    // int opt_active = 1;
    // if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt_active, sizeof(opt_active)) < 0)//allow quick reuse of port, bypassing TIME_WAIT limitations
    // {
    //     close(server_fd);
    //     return -1;  
    // }
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

void handle_client(int client_fd)
{
    std::cout << "Handling client connection (fd: " << client_fd << ")\n";
    char buffer[1024];
    ssize_t bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received > 0)
    {
        buffer[bytes_received] = '\0';
        std::cout << "Received data:\n" << buffer << "\n";
    }
    else
    {
        std::cerr << "Failed to receive data from client (fd: " << client_fd << ")\n";
    }
    send(client_fd, "HTTP/1.1 200 OK\r\n\n", 17, 0);
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
    std::cout << "Server listening on port " << port << "\n";

    while (true)
    {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0)
            continue;
        handle_client(client_fd);
        close(client_fd);
    }
}

