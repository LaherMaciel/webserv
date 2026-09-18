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

class Router
{
    public:
        Router(ServerConfig &config);
        ~Router();
        RouteType routeRequest(const Request& request, Response& response);
        LocationConfig *findLocation(const std::string &path);
        bool isValidMethod(const std::string &method, const LocationConfig *location);

    private:
        Router(const Router& other);
        Router& operator=(const Router& other);
        
        std::string mapFilePath(const std::string &path, const LocationConfig *location);
        std::string contentType(const std::string &path);
        ServerConfig &config_;
};


#endif