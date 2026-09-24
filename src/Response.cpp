#include "Response.hpp"
#include "webserv.hpp"

Response::Response() : statusCode_(500), version_("HTTP/1.1"), body_("")
{
    setHeader("Content-Length", "0");
    setHeader("Connection", "close");
}

Response::Response(int statusCode, const std::string &version)
    : statusCode_(statusCode), version_(version)
{
    setHeader("Content-Length", "0");
    setHeader("Connection", "close");
}

Response::Response(int statusCode, const std::string &version, const std::string &body, const std::string &contentType)
    : statusCode_(statusCode), version_(version), body_(body)
{
    setHeader("Content-Type", contentType);
    setHeader("Content-Length", toString(body.size()));
    setHeader("Connection", "close");
}

Response::~Response() {}

Response::Response(const Response& other)
    : statusCode_(other.statusCode_), version_(other.version_), body_(other.body_), headers_(other.headers_) {}

Response& Response::operator=(const Response& other)
{
    if (this != &other)
    {
        statusCode_ = other.statusCode_;
        version_ = other.version_;
        body_ = other.body_;
        headers_ = other.headers_;
    }
    return *this;
}

void Response::setBody(const std::string& body, const std::string& contentType)
{
    body_ = body;
    setHeader("Content-Type", contentType);
    setHeader("Content-Length", toString(body.size()));
}

void Response::setHeader(const std::string& key, const std::string& value) { headers_[key] = value; }

void Response::setStatusCode(int statusCode) { statusCode_ = statusCode; }

std::string Response::serialize() const
{
    //1) HTTP-Version SP Status-Code SP Reason-Phrase \r\n
    std::string response = version_ + " " + toString(statusCode_) + " " + httpReasonPhrase(statusCode_) + "\r\n";
    //2) Headers
    for (std::map<std::string, std::string>::const_iterator it = headers_.begin(); it != headers_.end(); ++it)
        response += it->first + ": " + it->second + "\r\n";
    //3) Blank line
    response += "\r\n";
    //4) Body (if any)
    response += body_;
    return response;
}
