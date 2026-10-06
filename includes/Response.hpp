#ifndef RESPONSE_HPP
#define RESPONSE_HPP

#include <string>
#include <map>

class Response
{
    public:
        Response();
        Response(int statusCode, const std::string &version);
        Response(int statusCode, const std::string &version, const std::string &body, const std::string &contentType);
        Response(const Response& other);
        Response& operator=(const Response& other);
        ~Response();

        void setBody(const std::string& body, const std::string& contentType = "text/plain");
        void setHeader(const std::string& key, const std::string& value);
        void setHeaders(const std::map<std::string, std::string>& headers);
        void setStatusCode(int statusCode);
        void setStatusCode(int statusCode, const std::string& reasonPhrase);

        std::string serialize() const;

    private:
        int statusCode_;
        std::string reasonPhrase_;
        std::string version_;
        std::string body_;
        std::map<std::string, std::string> headers_;
};

#endif