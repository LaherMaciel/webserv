#include "Request.hpp"

Request::Request() : method_(""), path_(""), version_(""), headers_() {}

Request::Request(const std::string &method, const std::string &path, const std::string &version, const std::map<std::string, std::string> &headers)
    : method_(method), path_(path), version_(version), headers_(headers) {}

Request::Request(const Request& other)
    : method_(other.method_), path_(other.path_), version_(other.version_), headers_(other.headers_) {}

Request& Request::operator=(const Request& other)
{
    if (this != &other)
    {
        method_ = other.method_;
        path_ = other.path_;
        version_ = other.version_;
        headers_ = other.headers_;
    }
    return *this;
}

const std::string& Request::getMethod() const { return method_; }

const std::string& Request::getPath() const { return path_; }

const std::map<std::string, std::string>& Request::getHeaders() const { return headers_; }

const std::string& Request::getVersion() const { return version_; }

Request::~Request() {}
