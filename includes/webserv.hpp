#ifndef WEBSERV_HPP
# define WEBSERV_HPP

#define DEFAULT_PORT 8080
#define MAX_CONNECTIONS 10
#define MAX_PENDING_CONNECTIONS 10

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <poll.h>
#include <cstddef>

const size_t IO_CHUNK_SIZE = 4096;
    
class Connection;

int 	set_non_blocking(int fd);

//utils.cpp
std::string toLower(std::string s);
std::string toString(size_t value);
bool readFile(const std::string& path, std::string& content);
std::string httpReasonPhrase(int code);

#endif
