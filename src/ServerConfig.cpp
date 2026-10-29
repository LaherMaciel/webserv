#include "ServerConfig.hpp"

ServerConfig::ServerConfig(): port_(0), host_("0.0.0.0")
{
    maxBodySize_ = 1000000;
}

LocationConfig::LocationConfig(): index_("index.html"), autoindex_(false),
    returnCode_(0), maxBodySize_(0), maxBodySizeSet_(false) {}
