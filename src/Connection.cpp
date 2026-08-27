

#include "Connection.hpp"

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
