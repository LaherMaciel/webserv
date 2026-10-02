#ifndef ROUTER_HPP
# define ROUTER_HPP
# include <string>

class Response;
class Request;
struct ServerConfig;
struct LocationConfig;

enum RouteType
{
    ROUTE_STATIC,
    ROUTE_CGI,
    ROUTE_ERROR
};

struct CgiInfo
{
    std::string scriptFilename_;//file name e.g. search.py
    std::string scriptUrlPath_;//URL path e.g. /cgi-bin/search.py
    std::string scriptFilesystemPath_;//disk location e.g. ./www/cgi-bin/search.py
    std::string pathInfo_;//extra path after script e.g. /users/42
    std::string interpreterPath_;//program that runs script e.g. /usr/bin/python3
    std::string workingDirectory_;//directory where script runs e.g. ./www/cgi-bin
};

class Router
{
    public:
        Router(ServerConfig &config);
        ~Router();
        RouteType routeRequest(const Request& request, Response& response, CgiInfo &cgiInfo);
        LocationConfig *findLocation(const std::string &path);
        bool isValidMethod(const std::string &method, const LocationConfig *location);
        bool splitCgiPath(CgiInfo &info, const std::string &path, const std::string &extension);
        void completeCGIinfo(CgiInfo &cgiInfo, const Request &request, const LocationConfig *location);

    private:
        Router(const Router& other);
        Router& operator=(const Router& other);
        
        std::string mapFilePath(const std::string &path, const LocationConfig *location);
        std::string contentType(const std::string &path);
        ServerConfig &config_;
};


#endif
