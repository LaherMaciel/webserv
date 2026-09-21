#ifndef SERVER_CONFIG_HPP
# define SERVER_CONFIG_HPP

#include "webserv.hpp"
#include <string>
#include <vector>

struct LocationConfig
{
    std::string path_;
    std::string root_;
    std::string index_;
    std::vector <std::string> allowedMethods_;
    std::map<std::string, std::string> cgiHandlers_;
};

struct ServerConfig
{
    int         port_;
    std::string host_;
    std::string serverName_;
    size_t      maxBodySize_;
    // std::map<int, std::string>  errorPages_;
    std::vector<LocationConfig> locations_;
};

#endif