#ifndef REQUEST_HPP
#define REQUEST_HPP

#include <string>
#include <map>

class Request
{
    public:
        Request();
        Request(const std::string &method, const std::string &path, const std::string &version, const std::map<std::string, std::string> &headers);//temp
        ~Request();
        Request(const Request& other);
        Request& operator=(const Request& other);

        const std::string& getMethod() const;
        const std::string& getPath() const;
        const std::string& getVersion() const;
        const std::map<std::string, std::string>& getHeaders() const;

    private:
        std::string method_;
        std::string path_;
        std::string version_;
        std::map<std::string, std::string> headers_;
};

#endif