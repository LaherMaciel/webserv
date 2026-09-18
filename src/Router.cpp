#include "Router.hpp"
#include "Response.hpp"
#include "Request.hpp"
#include "webserv.hpp"
#include "Server.hpp"
#include "ServerConfig.hpp"
#include <iostream>

Router::Router(ServerConfig &config) : config_(config) {}

Router::~Router() {}

std::string Router::mapFilePath(const std::string &path, const LocationConfig *location)
{
    if (!location)
        return "";
    if (path == location->path_ && !location->index_.empty())
        return location->root_ + "/index.html";
    else if (path.find(location->path_) == 0)
        return location->root_ + path.substr(location->path_.size());
    else
        return "";
}

std::string Router::contentType(const std::string &path)
{
    size_t slash = path.rfind('/');
    size_t dot = path.rfind('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return "application/octet-stream";
    std::string ext = path.substr(dot + 1);
    if (ext == "html")
        return "text/html";
    else if (ext == "jpg")
        return "image/jpeg";
    else
        return "application/octet-stream";
}

LocationConfig *Router::findLocation(const std::string &path)
{
    for (size_t i = 0; i < config_.locations_.size(); ++i)
    {
        if (path.find(config_.locations_[i].path_) == 0)
            return &config_.locations_[i];
    }
    return NULL;
}

bool Router::isValidMethod(const std::string &method, const LocationConfig *location)
{
    for (size_t i = 0; i < location->allowedMethods_.size(); ++i)
    {
        if (method == location->allowedMethods_[i])
            return true;
    }
    return false;
}

RouteType Router::routeRequest(const Request& request, Response& response)
{
    if (config_.locations_.empty())
    {
        std::cerr << "No locations configured for the server\n";
        response = Response(500, request.getVersion());
        return ROUTE_ERROR;
    }
    LocationConfig *location = findLocation(request.getPath());
    if (!location)
    {
        std::cout << "No matching location for path: " << request.getPath() << "\n";
        response = Response(404, request.getVersion());
        return ROUTE_ERROR;
    }
    if (!isValidMethod(request.getMethod(), location))
    {
        std::cout << "Unsupported method: " << request.getMethod() << "\n";
        response = Response(405, request.getVersion());
        return ROUTE_ERROR;
    }
    if (request.getPath().find("?") != std::string::npos)
    {
        return ROUTE_CGI;//TEMPORARY - not correct
    }
    std::string path = mapFilePath(request.getPath(), location);
    if (path.empty())
    {
        std::cout << "Unsupported path: " << request.getPath() << "\n";
        response = Response(404, request.getVersion());
        return ROUTE_ERROR;
    }
    std::cout << "Routing GET request for path: " << request.getPath() << "\n";
    std::string body;
    if (!readFile(path, body))
    {
        response = Response(500, request.getVersion());
        std::cerr << "Error reading " << path << "\n";
        return ROUTE_ERROR;
    }
    response = Response(200, request.getVersion(), body, contentType(path));
    return ROUTE_STATIC;
}

