#include "Router.hpp"
#include "Response.hpp"
#include "Request.hpp"
#include "webserv.hpp"
#include <iostream>

Router::Router() {}

Router::~Router() {}

std::string Router::mapFilePath(const std::string &path)
{
    if (path == "/")
        return "www/index.html";
    else if (path == "/about" || path == "/about.html" || path == "/about/")
        return "www/about.html";
    else if (path == "/dog.jpg")
        return "www/dog.jpg";
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

Response Router::routeRequest(const Request& request)
{
    if (request.getMethod() != "GET")
    {
        std::cout << "Unsupported method: " << request.getMethod() << "\n";
        return Response(405, request.getVersion());
    }
    std::string path = mapFilePath(request.getPath());
    if (path.empty())
    {
        std::cout << "Unsupported path: " << request.getPath() << "\n";
        return Response(404, request.getVersion());
    }
    std::cout << "Routing GET request for path: " << request.getPath() << "\n";
    std::string body;
    if (!readFile(path, body))
    {
        std::cerr << "Error reading " << path << "\n";
        return Response(500, request.getVersion());
    }
    Response response(200, request.getVersion(), body, contentType(path));
    return response;
}
