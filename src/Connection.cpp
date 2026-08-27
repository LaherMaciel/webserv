
#include "Connection.hpp"
#include <unistd.h>
#include <iostream>
#include <sys/socket.h> //for recv() send()
#include <cerrno>//for errno

Connection::Connection() : _fd(-1), in_buffer("") {}

Connection::Connection(int fd) : _fd(fd), in_buffer("") {}

Connection::~Connection() {}

Connection::Connection(const Connection& other) : _fd(other._fd), in_buffer(other.in_buffer) {}

Connection& Connection::operator=(const Connection& other)
{
    if (this != &other)
    {
        _fd = other._fd;
        in_buffer = other.in_buffer;
    }
    return *this;
}

int Connection::handle_client()
{
    // client.fd->revents = 0;//reset revents to 0 - in calling function?
    std::cout << "Handling client connection (fd: " << _fd << ")\n";
    char buffer[1024];
    ssize_t bytes_received = recv(_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received == 0)
    {
        std::cout << "Client disconnected (fd: " << _fd << ")\n";
        close(_fd);
        return -1;
    }
    else if (bytes_received < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        std::cerr << "Error receiving data from client (fd: " << _fd << ")\n";
        close(_fd);
        return -1;
    }
    else
    {
        buffer[bytes_received] = '\0';
        std::cout << "Received data:\n" << buffer << "\n";
        in_buffer.append(buffer, bytes_received);
        send(_fd, "HTTP/1.1 200 OK\r\n\r\n", 17, 0);
    }
    return 0;
}
