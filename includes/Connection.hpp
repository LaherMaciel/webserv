#ifndef CONNECTION_HPP
# define CONNECTION_HPP

#include <iostream>
#include <string>
//why a std::string and not. char like the previews?
class Connection
{
	public:
	int			_fd;
	std::string	in_buffer;
	Connection();
    Connection(int fd);
    ~Connection();
    Connection(const Connection& other);
    Connection& operator=(const Connection& other);
    int handle_client();
};

#endif