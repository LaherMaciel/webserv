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
    int handle_client();

    private: //prohibits copy construct or copy assign, so we don't need to create functions
    Connection(const Connection& other);
    Connection& operator=(const Connection& other);
};

#endif