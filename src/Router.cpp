#include "Router.hpp"
#include "Response.hpp"
#include "Request.hpp"
#include "webserv.hpp"
#include "Server.hpp"
#include "ServerConfig.hpp"
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <cerrno>
#include <dirent.h>

Router::Router(ServerConfig &config) : config_(config) {}

Router::~Router() {}

static void getPathInfo(const std::string &path, struct stat &fileInfo, int missingCode)
{
    if (stat(path.c_str(), &fileInfo) == 0)
        return;
    int error = errno;
    if (error == EACCES || error == EPERM)
        throw 403;
    if (error == ENOENT || error == ENOTDIR)
        throw missingCode;// 404 for files, 403 for directories
    throw 500;
}

void Router::validateUrlPath(const std::string &path)
{
    if (path.empty() || path[0] != '/')
        throw 400;
    for (size_t i = 0; i < path.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(path[i]);
        if (c < 32 || c >= 127)
            throw 400;
    }
    size_t segmentStart = 1;
    while (segmentStart <= path.size())
    {
        size_t segmentEnd = path.find('/', segmentStart);
        if (segmentEnd == std::string::npos)
            segmentEnd = path.size();
        std::string segment = path.substr(segmentStart, segmentEnd - segmentStart);
        if (segment == "." || segment == "..")
            throw 400;
        if (segmentEnd == path.size())
            break;
        segmentStart = segmentEnd + 1;
    }
}

std::string Router::mapRootPath(const std::string &path, const LocationConfig *location)
{
    if (!location || location->root_.empty())
        return "";
    if (path.compare(0, location->path_.size(), location->path_) != 0)
        return "";
    std::string suffix = path.substr(location->path_.size());
    if (suffix.empty())
        return location->root_;
    bool rootEndsWithSlash = location->root_[location->root_.size() - 1] == '/';
    bool suffixStartsWithSlash = suffix[0] == '/';
    if (rootEndsWithSlash && suffixStartsWithSlash)
        return location->root_ + suffix.substr(1);
    if (!rootEndsWithSlash && !suffixStartsWithSlash)
        return location->root_ + "/" + suffix;
    return location->root_ + suffix;
}

LocationConfig *Router::findLocation(const std::string &path)
{
    LocationConfig *bestMatch = NULL;
    for (size_t i = 0; i < config_.locations_.size(); ++i)
    {
        LocationConfig &location = config_.locations_[i];
        if (location.path_.empty() || path.find(location.path_) != 0)
            continue;
        bool endsAtBoundary = (path.size() == location.path_.size() || location.path_ == "/" || 
            location.path_[location.path_.size() - 1] == '/' || path[location.path_.size()] == '/');
        if (endsAtBoundary && (!bestMatch || location.path_.size() > bestMatch->path_.size()))
            bestMatch = &location;
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
    std::cout << "Script Filename: " << cgiInfo.scriptFilename_ << "\n";
    std::cout << "Server Name: " << cgiInfo.serverName_ << "\n";
    std::cout << "Server Port: " << cgiInfo.serverPort_ << "\n";
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
            info.pathInfo_ = path.substr(scriptEnd);
            return true;
        }
        pos = path.find(extension, pos + 1);
    }
    return false;
}

void Router::validateCgiScript(const std::string &scriptPath)
{
    struct stat fileInfo;
    if (stat(scriptPath.c_str(), &fileInfo) == -1)
    {
        if (errno == EACCES)
            throw 403;
        throw 404;
    }
    if (!S_ISREG(fileInfo.st_mode))
        throw 404;
    if (access(scriptPath.c_str(), R_OK) == -1)
        throw 403;
}

//for cgi environment vars
std::string Router::getPrimaryServerName(const std::map<std::string, std::string> &requestHeaders)
{
    std::string serverName;
    if (!config_.serverName_.empty())
        serverName = config_.serverName_[0];
    else
    {
        std::map<std::string, std::string>::const_iterator it = requestHeaders.find("host");
        size_t colon;
        if (it != requestHeaders.end() && (colon = it->second.find(':')) != std::string::npos)
            serverName = it->second.substr(0, colon);
        else if (it != requestHeaders.end())
            serverName = it->second;
        else
            serverName = config_.host_;
    }
    return serverName;
}

void Router::completeCGIinfo(CgiInfo &cgiInfo, const Request &request, const LocationConfig *location)
{
    const std::map<std::string, std::string> &handlers = location->cgiHandlers_;
    bool found = false;
    size_t BestExtLength = 0;
    size_t BestScriptEnd = std::string::npos;
    for (std::map<std::string, std::string>::const_iterator it = handlers.begin(); it != handlers.end(); ++it)
    {
        const std::string &extension = it->first;
        CgiInfo info;
        if (splitCgiPath(info, request.getPath(), extension))
        {
            size_t extLength = extension.size();
            size_t scriptEnd = info.scriptUrlPath_.size();
            if (!found || scriptEnd < BestScriptEnd || (scriptEnd == BestScriptEnd && extLength > BestExtLength))
            {
                found = true;
                BestExtLength = extLength;
                BestScriptEnd = scriptEnd;
                info.interpreterPath_ = it->second;
                info.scriptFilesystemPath_ = mapRootPath(info.scriptUrlPath_, location);
                size_t slash = info.scriptFilesystemPath_.rfind('/');
                info.workingDirectory_ = info.scriptFilesystemPath_.substr(0, slash);
                info.scriptFilename_ = info.scriptFilesystemPath_.substr(slash + 1);
                info.serverName_ = getPrimaryServerName(request.getHeaders());
                info.serverPort_ = toString(config_.port_);
                cgiInfo = info;
            }
        }
    }
    if (!found)
        throw 404;
    printCgiInfo(cgiInfo);
}

RouteType Router::routeCGI(const Request& request, const LocationConfig *location, CgiInfo &cgiInfo)
{
    completeCGIinfo(cgiInfo, request, location);
    validateCgiScript(cgiInfo.scriptFilesystemPath_);
    return ROUTE_CGI;
}

void Router::writeToFile(const std::string &uploadPath, const std::string &body)
{
    std::ofstream uploadFile(uploadPath.c_str(), std::ios::binary);
    if (!uploadFile.is_open())
        throw 500;
    uploadFile << body;
    if (!uploadFile.good())
        throw 500;
    uploadFile.close();
}

std::string Router::mapUploadPath(const LocationConfig *location, const std::string &urlPath)
{
    if (location->uploadStore_.empty())
        throw 403;
    std::string fileName = urlPath.substr(location->path_.size());
    if (!location->path_.empty() && location->path_[location->path_.size() - 1] != '/')
    {
        if (fileName.empty() || fileName[0] != '/')
            throw 400;
        fileName.erase(0, 1);
    }
    if (fileName.empty() || fileName.find('/') != std::string::npos)
        throw 400;
    std::string uploadPath = location->uploadStore_ + "/" + fileName;
    return uploadPath;
}

/*
printf 'hello from upload\nsecond line\n' > /tmp/webserv-upload.txt
curl -i --data-binary @/tmp/webserv-upload.txt http://127.0.0.1:8080/upload/hello.txt
*/
RouteType Router::routeUpload(const Request& request, const LocationConfig *location, Response& response)
{
    std::cout << "Routing POST request for path: " << request.getPath() << "\n";
    std::string uploadPath = mapUploadPath(location, request.getPath());
    writeToFile(uploadPath, request.getBody());
    response = Response(201, request.getVersion(), "POST request received", "text/plain");
    response.setHeader("Location", request.getPath());
    return ROUTE_UPLOAD;
}
 /*
ENOENT: the file or a path component does not exist.
ENOTDIR: a path component expected to be a directory is not one.
EACCES: filesystem permissions deny access.
EPERM: the operation itself is not permitted.
EROFS: the filesystem is read-only.
 */
void Router::removeFile(const std::string &filePath)
{
    if (unlink(filePath.c_str()) == 0)
        return ;
    int error = errno;
    if (error == EACCES || error == EPERM || error == EROFS || error == EISDIR)
        throw 403;
    if (error == ENOENT || error == ENOTDIR)
        throw 404;
    throw 500;
}

RouteType Router::routeDelete(const Request& request, const LocationConfig *location, Response& response)
{
    std::cout << "Routing DELETE request for path: " << request.getPath() << "\n";
    std::string filePath;
    if (!location->uploadStore_.empty())
        filePath = mapUploadPath(location, request.getPath());
    else
        filePath = mapRootPath(request.getPath(), location);
    if (filePath.empty())
        throw 403;
    removeFile(filePath);
    response = Response(200, request.getVersion(), "DELETE request received", "text/plain");
    return ROUTE_DELETE;
}

RouteType Router::serveFile(const Request& request, const std::string &filePath, Response& response)
{
    std::string body;
    if (!readFile(filePath, body))
    {
        std::cerr << "Error reading " << filePath << "\n";
        throw 500;
    }
    response = Response(200, request.getVersion(), body, contentType(filePath));
    return ROUTE_STATIC;
}

std::vector<std::string> Router::listDirectoryEntries(const std::string &directoryPath)
{
    std::vector<std::string> entries;
    DIR *dir = opendir(directoryPath.c_str());
    if (!dir)
    {
        int error = errno;
        if (error == EACCES || error == EPERM)
            throw 403;
        if (error == ENOENT || error == ENOTDIR)
            throw 404;
        throw 500;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        std::string name = entry->d_name;
        if (name == "." || name == "..")
            continue;
        entries.push_back(name);
    }
    closedir(dir);
    return entries;
}

static std::string htmlEscape(const std::string &value)
{
    std::string result;

    for (size_t i = 0; i < value.size(); ++i)
    {
        switch (value[i])
        {
            case '&':  result += "&amp;";  break;
            case '<':  result += "&lt;";   break;
            case '>':  result += "&gt;";   break;
            case '"':  result += "&quot;"; break;
            case '\'': result += "&#39;";  break;
            default:   result += value[i];
        }
    }
    return result;
}

void Router::generateAutoindex(const std::string &directoryPath, Response& response, const Request& request)
{
    std::vector<std::string> entries = listDirectoryEntries(directoryPath);
    std::sort(entries.begin(), entries.end());
    std::string escapedPath = htmlEscape(request.getPath());
    std::string body = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
        "<title>Index of " + escapedPath + "</title></head><body>"
        "<h1>Index of " + escapedPath + "</h1><ul>";
    for (size_t i = 0; i < entries.size(); ++i)
    {
        struct stat fileInfo;
        std::string fullPath = directoryPath;
        if (!fullPath.empty() && fullPath[fullPath.size() - 1] != '/')
            fullPath += "/";
        fullPath += entries[i];
        if (stat(fullPath.c_str(), &fileInfo) == -1)
            continue;
        std::string escapedEntry = htmlEscape(entries[i]);
        if (S_ISDIR(fileInfo.st_mode))
            body += "<li><a href=\"" + escapedEntry + "/\">" + escapedEntry + "/</a></li>";
        else
            body += "<li><a href=\"" + escapedEntry + "\">" + escapedEntry + "</a></li>";
    }
    body += "</ul></body></html>";
    response = Response(200, request.getVersion(), body, "text/html");
}

RouteType Router::routeDirectory(const Request& request, const std::string &directoryPath,
                                 const LocationConfig *location, Response& response)
{
    std::string urlPath = request.getPath();
    if (urlPath[urlPath.size() - 1] != '/')
    {
        std::string target = urlPath + "/";
        if (!request.getQuery().empty())
            target += "?" + request.getQuery();
        return routeRedirect(request, response, 301, target);
    }
    std::string indexPath = directoryPath;
    if (!indexPath.empty() && indexPath[indexPath.size() - 1] != '/')
        indexPath += "/";
    indexPath += location->index_;
    struct stat indexInfo;
    try
    {
        getPathInfo(indexPath, indexInfo, 404);
        if (!S_ISREG(indexInfo.st_mode))
            throw 403;
        return serveFile(request, indexPath, response);
    }
    catch(int e)
    {
        if (e != 404)
            throw e;
    }
    if (!location->autoindex_)
        throw 403;
    generateAutoindex(directoryPath, response, request);
    return ROUTE_STATIC;
}

RouteType Router::routeRedirect(const Request& request, Response& response, int returnCode, const std::string &target)
{
    response = Response(returnCode, request.getVersion());
    response.setHeader("Location", target);
    return ROUTE_REDIRECT;
}

RouteType Router::routeGet(const Request& request, const LocationConfig *location, Response& response)
{
    std::cout << "Routing GET request for path: " << request.getPath() << "\n";
    std::string filePath = mapRootPath(request.getPath(), location);
    if (filePath.empty())
        throw 404;
    struct stat fileInfo;
    getPathInfo(filePath, fileInfo, 404);
    if (S_ISREG(fileInfo.st_mode))
        return serveFile(request, filePath, response);
    if (S_ISDIR(fileInfo.st_mode))
        return routeDirectory(request, filePath, location, response);
    throw 403;
}

RouteType Router::routeRequest(const Request& request, Response& response, CgiInfo &cgiInfo)
{
    validateUrlPath(request.getPath());
    LocationConfig *location = findLocation(request.getPath());
    if (!location)
        throw 404;
    if (location->maxBodySize_ != 0 && request.getBody().size() > location->maxBodySize_)
        throw 413;
    if (location->returnCode_ != 0)
        return routeRedirect(request, response, location->returnCode_, location->returnTarget_);
    if (!isValidMethod(request.getMethod(), location))
        throw 405;
    if (!location->cgiHandlers_.empty())
        return routeCGI(request, location, cgiInfo);
    if (request.getMethod() == "POST")
        return routeUpload(request, location, response);
    if (request.getMethod() == "DELETE")
        return routeDelete(request, location, response);
    if (request.getMethod() == "GET")
        return routeGet(request, location, response);
    throw 405;
}
