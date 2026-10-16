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
std::string ipv4LongToString(unsigned long ip);
std::string toLower(std::string s);
std::string toString(size_t value);
bool readFile(const std::string& path, std::string& content);
std::string quoteValue(const std::string &s);
std::string httpReasonPhrase(int code);
# define WHITESPACE " \t\n\r\v\f"
# define DEFAULT_CONFIG "config/default.conf"

size_t skipSpaces(const std::string &str, size_t pos);
void removeSpaceAfter(std::string &str);

#endif
