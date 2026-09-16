#ifndef ROUTER_HPP
# define ROUTER_HPP
# include <string>

class Response;
class Request;

class Router
{
    public:
        Router();
        ~Router();
        Response routeRequest(const Request& request);

    private:
        Router(const Router& other);
        Router& operator=(const Router& other);
        
        std::string mapFilePath(const std::string &path);
        std::string contentType(const std::string &path);
};


#endif