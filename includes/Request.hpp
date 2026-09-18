#ifndef REQUEST_HPP
#define REQUEST_HPP

#include <string>
#include <map>

class Request
{
    public:
        Request();
        ~Request();
        Request(const Request& other);
        Request& operator=(const Request& other);

        void setMethod(const std::string& method);
        void setPath(const std::string& path);
        void setVersion(const std::string& version);
        void setHeaders(const std::map<std::string, std::string>& headers);
        void setHaveBody(bool haveBody);
        void setIsBodyComplete(bool bodyComplete);
        void setBody(const std::string& body);
        void appendToBody(const std::string& body);

        const std::string& getMethod() const;
        const std::string& getPath() const;
        const std::string& getVersion() const;
        const std::string& getBody() const;
        const std::map<std::string, std::string>& getHeaders() const;
        bool getIsBodyComplete() const;

        void printRequest() const; // Test

    private:
        std::string method_;
        std::string path_;
        std::string version_;
        std::string body_;
        std::map<std::string, std::string> headers_;
        bool    haveBody_;
        bool    isBodyComplete_;
};

#endif