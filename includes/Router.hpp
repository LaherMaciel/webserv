#ifndef ROUTER_HPP
# define ROUTER_HPP
# include <string>

class Response;
class Request;

enum RouteType
{
    ROUTE_STATIC,
    ROUTE_CGI,
    ROUTE_ERROR
};

class Router
{
    public:
        Router();
        ~Router();
        RouteType routeRequest(const Request& request, Response& response);

    private:
        Router(const Router& other);
        Router& operator=(const Router& other);
        
        std::string mapFilePath(const std::string &path);
        std::string contentType(const std::string &path);
};


#endif