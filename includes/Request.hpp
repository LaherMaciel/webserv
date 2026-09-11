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

        void setMethod(const std::string& method);
        void setPath(const std::string& path);
        void setVersion(const std::string& version);
        void setHeaders(const std::map<std::string, std::string>& headers);

        const std::string& getMethod() const;
        const std::string& getPath() const;
        const std::string& getVersion() const;
        const std::map<std::string, std::string>& getHeaders() const;

        void printRequest() const; // Test

    private:
        std::string method_;
        std::string path_;
        std::string version_;
        std::map<std::string, std::string> headers_;
};

#endif