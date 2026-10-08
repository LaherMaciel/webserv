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

Router::Router(ServerConfig &config) : config_(config) {}

Router::~Router() {}

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
    return location->root_ + path;
}

std::string Router::mapFilePath(const std::string &path, const LocationConfig *location)
{
    std::string filePath = mapRootPath(path, location);
    if (filePath.empty())
        return "";
    if (path == location->path_ && !location->index_.empty())
    {
        if (filePath.empty() || filePath[filePath.size() - 1] != '/')
            filePath += "/";
        filePath += location->index_;
    }
    return filePath;
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
                info.scriptFilesystemPath_ = location->root_ + info.scriptUrlPath_;
                size_t slash = info.scriptFilesystemPath_.rfind('/');
                info.workingDirectory_ = info.scriptFilesystemPath_.substr(0, slash);
                info.scriptFilename_ = info.scriptFilesystemPath_.substr(slash + 1);
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

RouteType Router::routeGet(const Request& request, const LocationConfig *location, Response& response)
{
    std::cout << "Routing GET request for path: " << request.getPath() << "\n";
    std::string filePath = mapFilePath(request.getPath(), location);
    if (filePath.empty())
        throw 404;
    std::string body;
    if (!readFile(filePath, body))
    {
        std::cerr << "Error reading " << filePath << "\n";
        throw 500;
    }
    response = Response(200, request.getVersion(), body, contentType(filePath));
    return ROUTE_STATIC;
}

RouteType Router::routeRequest(const Request& request, Response& response, CgiInfo &cgiInfo)
{
    try
    {
        validateUrlPath(request.getPath());
        LocationConfig *location = findLocation(request.getPath());
        if (!location)
            throw 404;
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
    catch (int errorCode)
    {
        response = Response(errorCode, request.getVersion());
        return ROUTE_ERROR;
    }
}
