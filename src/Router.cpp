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
        return location->root_ + "/" + location->index_;
    else if (path.find(location->path_) == 0)
        return location->root_ + "/" + path.substr(location->path_.size());
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
    LocationConfig *bestMatch = NULL;
    for (size_t i = 0; i < config_.locations_.size(); ++i)
    {
        if (path.find(config_.locations_[i].path_) == 0 &&
            (!bestMatch || config_.locations_[i].path_.size() > bestMatch->path_.size()))
            bestMatch = &config_.locations_[i];
    }
    return bestMatch;
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

static void printCgiInfo(const CgiInfo &cgiInfo)
{
    std::cout << "CGI Info:\n";
    std::cout << "Script URL Path: " << cgiInfo.scriptUrlPath_ << "\n";
    std::cout << "Script Filesystem Path: " << cgiInfo.scriptFilesystemPath_ << "\n";
    std::cout << "Path Info: " << cgiInfo.pathInfo_ << "\n";
    std::cout << "Interpreter Path: " << cgiInfo.interpreterPath_ << "\n";
    std::cout << "Working Directory: " << cgiInfo.workingDirectory_ << "\n";
}

bool Router::splitCgiPath(CgiInfo &info, const std::string &path, const std::string &extension)
{
    if (extension.empty())
        return false;
    size_t pos = path.find(extension);
    while (pos != std::string::npos)
    {
        size_t scriptEnd = pos + extension.size();
        if (scriptEnd == path.size() || path[scriptEnd] == '/')
        {
            info.scriptUrlPath_ = path.substr(0, scriptEnd);
            info.scriptFilename_ = info.scriptUrlPath_.substr(info.scriptUrlPath_.find_last_of('/') + 1);
            info.pathInfo_ = path.substr(scriptEnd);
            return true;
        }
        pos = path.find(extension, pos + 1);
    }
    return false;
}

void Router::completeCGIinfo(CgiInfo &cgiInfo, const Request &request, const LocationConfig *location)
{
    const std::map<std::string, std::string> &handlers =
    location->cgiHandlers_;

    for (std::map<std::string, std::string>::const_iterator it = handlers.begin(); it != handlers.end(); ++it)
    {
        const std::string &extension = it->first;
        const std::string &handler = it->second;
        CgiInfo info;
        if (splitCgiPath(info, request.getPath(), extension))
        {
            info.interpreterPath_ = handler;
            info.scriptFilesystemPath_ = location->root_ + location->path_ + "/" + info.scriptFilename_;
            info.workingDirectory_ = location->root_ + location->path_;
            cgiInfo = info;
            printCgiInfo(cgiInfo);
            return;
        }
    }
}

RouteType Router::routeRequest(const Request& request, Response& response, CgiInfo &cgiInfo)
{
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
    if (location->path_ == "/cgi-bin")
    {
        std::cout << "Routing CGI request for path: " << request.getPath() << "\n";
        completeCGIinfo(cgiInfo, request, location);
        return ROUTE_CGI;
    }
    // if (request.getPath() == "/cgi-bin/hello.py")//temp hardcoded
    // {
    //     cgiInfo.scriptFilename_ = "hello.py";
    //     cgiInfo.scriptUrlPath_ = "/cgi-bin/hello.py";
    //     cgiInfo.scriptFilesystemPath_ = "./www/cgi-bin/hello.py"; //should be ./www/cgi-bin/hello.py
    //     cgiInfo.pathInfo_ = "";
    //     //cgiInfo.interpreterPath_ = "/Library/Frameworks/Python.framework/Versions/3.9/bin/python3";
    //     //cgiInfo.interpreterPath_ = "/bin/python3.10";
    //     cgiInfo.interpreterPath_ = "/usr/bin/python";
    //     cgiInfo.workingDirectory_ = "cgi-bin";
    //     return ROUTE_CGI;
    // }
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
