#include "Request.hpp"
#include <iostream>

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

void Request::setMethod(const std::string& method) { method_ = method; }

void Request::setPath(const std::string& path) { path_ = path; }

void Request::setVersion(const std::string& version) { version_ = version; }

void Request::setHeaders(const std::map<std::string, std::string>& headers) { headers_ = headers; }

void Request::printRequest() const
{
    std::cout << "*REQUEST*\n";
    std::cout << "Method: " << method_ << "\n";
    std::cout << "Path: " << path_ << "\n";
    std::cout << "Version: " << version_ << "\n";
    std::cout << "Headers:\n";
    for (std::map<std::string, std::string>::const_iterator it = headers_.begin(); it != headers_.end(); ++it)
        std::cout << it->first << ": " << it->second << "\n";
    std::cout << "*END REQUEST*\n";
}

const std::string& Request::getMethod() const { return method_; }

const std::string& Request::getPath() const { return path_; }

const std::map<std::string, std::string>& Request::getHeaders() const { return headers_; }

const std::string& Request::getVersion() const { return version_; }

Request::~Request() {}
