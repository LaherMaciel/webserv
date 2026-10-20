#ifndef SERVER_CONFIG_HPP
# define SERVER_CONFIG_HPP

#include "webserv.hpp"
#include <string>
#include <vector>

struct LocationConfig
{
    std::string                         path_;
    std::string                         root_;
    std::string                         index_;
    std::vector<std::string>            allowedMethods_;
    std::map<std::string, std::string>  cgiHandlers_;
    bool                                autoindex_;
    std::string                         uploadStore_;
    int                                 returnCode_;
    std::string                         returnTarget_;
    size_t                              maxBodySize_;
    bool                                maxBodySizeSet_;

    LocationConfig();
};

struct ServerConfig
{
    size_t                      port_;
    std::string                 host_;
    std::vector<std::string>    serverName_;
    size_t                      maxBodySize_;
    std::map<int, std::string>  errorPages_;
    std::vector<LocationConfig> locations_;

    ServerConfig();
};

/* one "location <path> { <body> }" block, as cut out of a server body */
struct LocationBlock
{
    std::string path_;
    std::string body_;
};

/* one "<name> <args>;" line */
struct Directive
{
    std::string name_;
    std::string args_;
};

void                        stripComments(std::string &configFile);
void                        applyServerDirective(ServerConfig &server, const Directive &directive);
void                        applyLocationDirective(LocationConfig &location, const Directive &directive);
void                        printConfig(const std::vector<ServerConfig> &configs);
size_t                      findBlockEnd(const std::string &text, size_t open);
std::vector<std::string>    splitServerBlocks(const std::string &text);
std::vector<LocationBlock>  extractLocations(std::string &serverBody);
std::vector<Directive>      splitDirectives(const std::string &body);
std::vector<ServerConfig>   parseConfig(const std::string &path);

#endif