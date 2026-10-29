#include "ServerConfig.hpp"
#include "webserv.hpp"
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <climits>
#include <cerrno>

static size_t parseSize(const std::string &args, const std::string &name)
{
    char    *end;
    long    value;

    if (args.empty())
        throw std::runtime_error("config: '" + name + "' needs a value");
    errno = 0;
    value = std::strtol(args.c_str(), &end, 10);
    if (*end != '\0' || value < 0 || errno == ERANGE)
        throw std::runtime_error("config: '" + name
            + "' wants a positive number, got " + quoteValue(args));
    return (static_cast<size_t>(value));
}

static std::vector<std::string> splitTokens(const std::string &args)
{
    std::vector<std::string>    tokens;
    size_t                      pos = 0;

    while ((pos = args.find_first_not_of(WHITESPACE, pos)) != std::string::npos)
    {
        size_t end = args.find_first_of(WHITESPACE, pos);

        if (end == std::string::npos)
            end = args.size();
        tokens.push_back(args.substr(pos, end - pos));
        pos = end;
    }
    return (tokens);
}

static bool isValidHost(const std::string &host)
{
    size_t  pos = 0;
    int     parts = 0;

    if (host == "localhost")
        return (true);
    while (parts < 4)
    {
        size_t      dot = host.find('.', pos);
        std::string part;

        if (dot == std::string::npos)
            part = host.substr(pos);
        else
            part = host.substr(pos, dot - pos);

        if (part.empty() || part.size() > 3
            || part.find_first_not_of("0123456789") != std::string::npos)
            return (false);

        int value = 0;

        for (size_t k = 0; k < part.size(); ++k)
            value = value * 10 + (part[k] - '0');
        if (value > 255)
            return (false);
        parts++;
        if (dot == std::string::npos)
            return (parts == 4);
        pos = dot + 1;
    }
    return (false);
}

void applyServerDirective(ServerConfig &server, const Directive &directive)
{
    std::string   name = directive.name_;
    std::string   args = directive.args_;

    if (name == "listen")
    {
        size_t port = parseSize(args, name);
        if (port < 1 || port > 65535)
            throw std::runtime_error("config: 'listen' port out of range "
                "(1-65535), got " + quoteValue(args));
        server.port_ = port;
    }
    else if (name == "host")
    {
        if (args.empty() || args.find_first_of(WHITESPACE) != std::string::npos)
            throw std::runtime_error("config: 'host' wants one address, got "
                + quoteValue(args));
        if (!isValidHost(args))
            throw std::runtime_error("config: 'host' wants an IPv4 address or "
                "'localhost', got " + quoteValue(args));
        server.host_ = args;
    }
    else if (name == "server_name")
    {
        size_t pos = 0;
        std::string token = "";

        if (args.empty())
            throw std::runtime_error("config: 'server_name' needs a value");
        while ((pos = args.find_first_not_of(WHITESPACE, pos)) != std::string::npos)
        {
            size_t      tokenEnd = args.find_first_of(WHITESPACE, pos);
            if (tokenEnd == std::string::npos)
            {
                token = args.substr(pos);
                server.serverName_.push_back(token);
                break ;
            }
            token = args.substr(pos, tokenEnd - pos);
            server.serverName_.push_back(token);
            pos = tokenEnd;
        }
    }
    else if (name == "client_max_body_size")
        server.maxBodySize_ = parseSize(args, name);
    else if (name == "error_page")
    {
        size_t pathStart = args.find_last_of(WHITESPACE);
        size_t pos = 0;

        if (pathStart == std::string::npos)
            throw std::runtime_error("config: 'error_page' wants one or more "
                "codes then a path, got " + quoteValue(args));
        std::string path = args.substr(pathStart + 1);
        while ((pos = args.find_first_not_of(WHITESPACE, pos)) < pathStart)
        {
            size_t      tokenEnd = args.find_first_of(WHITESPACE, pos);
            std::string token = args.substr(pos, tokenEnd - pos);
            size_t      code = parseSize(token, name);

            if (code < 300 || code > 599)
                throw std::runtime_error("config: 'error_page' code out of "
                    "range (300-599), got " + quoteValue(token));
            if (server.errorPages_.find(static_cast<int>(code))
                != server.errorPages_.end())
                throw std::runtime_error("config: 'error_page' sets code "
                    + quoteValue(token) + " more than once");
            server.errorPages_[static_cast<int>(code)] = path;
            pos = tokenEnd;
        }
    }
    else
        throw std::runtime_error("config: unknown server directive '" + name + "'");
}

void applyLocationDirective(LocationConfig &location, const Directive &directive)
{
    std::string                 name = directive.name_;
    std::string                 args = directive.args_;
    std::vector<std::string>    tokens = splitTokens(args);

    if (name == "root" || name == "index" || name == "upload_store")
    {
        if (tokens.size() != 1)
            throw std::runtime_error("config: '" + name
                + "' wants one path, got " + quoteValue(args));
        if (name == "root")
            location.root_ = tokens[0];
        else if (name == "index")
            location.index_ = tokens[0];
        else
            location.uploadStore_ = tokens[0];
    }
    else if (name == "allowed_methods")
    {
        if (tokens.empty())
            throw std::runtime_error("config: 'allowed_methods' needs a value");
        location.allowedMethods_.clear();
        for (size_t i = 0; i < tokens.size(); ++i)
        {
            if (tokens[i] != "GET" && tokens[i] != "POST" && tokens[i] != "DELETE")
                throw std::runtime_error("config: 'allowed_methods' accepts only "
                    "GET, POST and DELETE, got " + quoteValue(tokens[i]));
            for (size_t j = 0; j < location.allowedMethods_.size(); ++j)
                if (location.allowedMethods_[j] == tokens[i])
                    throw std::runtime_error("config: 'allowed_methods' lists "
                        + quoteValue(tokens[i]) + " twice");
            location.allowedMethods_.push_back(tokens[i]);
        }
    }
    else if (name == "autoindex")
    {
        if (tokens.size() != 1 || (tokens[0] != "on" && tokens[0] != "off"))
            throw std::runtime_error("config: 'autoindex' wants on or off, got "
                + quoteValue(args));
        if (tokens[0] == "on")
            location.autoindex_ = true;
        else
            location.autoindex_ = false;
    }
    else if (name == "return")
    {
        if (tokens.size() != 2)
            throw std::runtime_error("config: 'return' wants a code and a target, "
                "got " + quoteValue(args));
        size_t code = parseSize(tokens[0], name);
        bool supported = code == 301 || code == 302 ||
            code == 307 || code == 308;
        if (!supported)
            throw std::runtime_error("config: unsupported redirect code "
                + quoteValue(tokens[0]));
        location.returnCode_ = static_cast<int>(code);
        location.returnTarget_ = tokens[1];
    }
    else if (name == "cgi_extension")
    {
        if (tokens.size() != 2)
            throw std::runtime_error("config: 'cgi_extension' wants an extension "
                "and an interpreter, got " + quoteValue(args));
        if (tokens[0][0] != '.' || tokens[0].size() < 2)
            throw std::runtime_error("config: 'cgi_extension' extension must be "
                "'.' followed by at least one character, got " + quoteValue(tokens[0]));
        if (location.cgiHandlers_.find(tokens[0]) != location.cgiHandlers_.end())
            throw std::runtime_error("config: 'cgi_extension' sets "
                + quoteValue(tokens[0]) + " more than once");
        location.cgiHandlers_[tokens[0]] = tokens[1];
    }
    else if (name == "client_max_body_size")
    {
        location.maxBodySize_ = parseSize(args, name);
        location.maxBodySizeSet_ = true;
    }
    else
        throw std::runtime_error("config: unknown location directive '" + name + "'");
}

static void checkDuplicates(const std::vector<Directive> &directives)
{
    for (size_t i = 0; i < directives.size(); ++i)
    {
        if (directives[i].name_ == "error_page"
            || directives[i].name_ == "cgi_extension")
            continue ;
        for (size_t j = i + 1; j < directives.size(); ++j)
        {
            if (directives[j].name_ == directives[i].name_)
                throw std::runtime_error("config: " + quoteValue(directives[i].name_)
                    + " appears more than once in the same block");
        }
    }
}

std::vector<ServerConfig> parseConfig(const std::string &path)
{
    std::vector<ServerConfig>   configs;
    std::string                 text;

    if (!readFile(path, text))
        throw std::runtime_error("config: cannot read " + quoteValue(path));

    stripComments(text);
    std::vector<std::string>    blocks = splitServerBlocks(text);

    for (size_t b = 0; b < blocks.size(); ++b)
    {
        ServerConfig                server;
        std::vector<LocationBlock>  locations = extractLocations(blocks[b]);
        std::vector<Directive>      directives = splitDirectives(blocks[b]);

        checkDuplicates(directives);
        for (size_t i = 0; i < directives.size(); ++i)
            applyServerDirective(server, directives[i]);
        if (server.port_ == 0)
            throw std::runtime_error("config: server block with no 'listen'");
        for (size_t l = 0; l < locations.size(); ++l)
        {
            LocationConfig          location;
            std::vector<Directive>  locDirectives;

            location.path_ = locations[l].path_;
            locDirectives = splitDirectives(locations[l].body_);
            checkDuplicates(locDirectives);
            for (size_t i = 0; i < locDirectives.size(); ++i)
                applyLocationDirective(location, locDirectives[i]);
            if (location.allowedMethods_.empty())
                location.allowedMethods_.push_back("GET");
            if (!location.maxBodySizeSet_)
                location.maxBodySize_ = server.maxBodySize_;
            if (location.root_.empty() && location.returnCode_ == 0)
                throw std::runtime_error("config: location " + location.path_
                    + " has neither 'root' nor 'return', so it can serve nothing");
            server.locations_.push_back(location);
        }
        if (server.locations_.empty())
            throw std::runtime_error("config: server block on port "
                + toString(server.port_) + " has no 'location'");
        configs.push_back(server);
    }
    return (configs);
}

void printConfig(const std::vector<ServerConfig> &configs)
{
    std::cout << "=== parsed " << configs.size() << " server block(s) ===" << std::endl;
    for (size_t b = 0; b < configs.size(); ++b)
    {
        const ServerConfig &sv = configs[b];

        std::cout << "server " << b << ": listen " << sv.port_
            << "  host " << sv.host_;
        for (size_t i = 0; i < sv.serverName_.size(); i++)
        {
            std::cout << "  server_name \"" << sv.serverName_[i] << "\"";
        }
        std:: cout << "  client_max_body_size ";
        if (sv.maxBodySize_ == 0)
            std::cout << "0 (unlimited)" << std::endl;
        else
            std::cout << sv.maxBodySize_ << std::endl;
        for (std::map<int, std::string>::const_iterator it = sv.errorPages_.begin();
            it != sv.errorPages_.end(); ++it)
            std::cout << "    error_page " << it->first << " " << it->second << std::endl;
        for (size_t l = 0; l < sv.locations_.size(); ++l)
        {
            const LocationConfig &lc = sv.locations_[l];

            std::cout << "    location " << lc.path_ << std::endl;
            std::cout << "        root            " << lc.root_ << std::endl;
            std::cout << "        index           " << lc.index_ << std::endl;
            std::cout << "        autoindex       ";
            if (lc.autoindex_)
                std::cout << "on" << std::endl;
            else
                std::cout << "off" << std::endl;
            std::cout << "        allowed_methods ";
            for (size_t i = 0; i < lc.allowedMethods_.size(); ++i)
            {
                if (i != 0)
                    std::cout << " ";
                std::cout << lc.allowedMethods_[i];
            }
            std::cout << std::endl;
            std::cout << "        max_body_size   ";
            if (lc.maxBodySize_ == 0)
                std::cout << "0 (unlimited)" << std::endl;
            else
                std::cout << lc.maxBodySize_ << std::endl;
            if (!lc.uploadStore_.empty())
                std::cout << "        upload_store    " << lc.uploadStore_ << std::endl;
            if (lc.returnCode_ != 0)
                std::cout << "        return          " << lc.returnCode_
                    << " " << lc.returnTarget_ << std::endl;
            for (std::map<std::string, std::string>::const_iterator it = lc.cgiHandlers_.begin();
                it != lc.cgiHandlers_.end(); ++it)
                std::cout << "        cgi_extension   " << it->first
                    << " " << it->second << std::endl;
        }
    }
}
