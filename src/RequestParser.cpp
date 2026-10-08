#include "RequestParser.hpp"
#include "webserv.hpp"
#include "Request.hpp"
#include <cstdlib>

RequestParser::RequestParser() : endOfHeaders_(0), status_(PARSE_INCOMPLETE) {}
RequestParser::~RequestParser() {}

void RequestParser::validateRequestLine(const std::string &method, const std::string &path, const std::string &version)
{
    if (method != "GET" && method != "POST" && method != "DELETE")// add all the methods allowed by HTTP/1.1
    {
        throw 400;
    }
    if (path.empty() || path[0] != '/')
    {
        throw 400;
    }
    if (version != "HTTP/1.1" && version != "HTTP/1.0")
    {
        throw 400;
    }
}

bool RequestParser::validateRequestTarget(const std::string &target)
{
    for (size_t i = 0; i < target.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(target[i]);
        if (c < 32 || c == 127 || c == '#')
            return false;
        if (c == '%')
        {
            if (i + 2 >= target.size())
                return false;
            if (!std::isxdigit(
                    static_cast<unsigned char>(target[i + 1])) ||
                !std::isxdigit(
                    static_cast<unsigned char>(target[i + 2])))
                return false;
            i += 2;
        }
    }
    return true;
}

std::string RequestParser::parseQuery(std::string &requestTarget)
{
    size_t query_pos = requestTarget.find('?');
    if (query_pos == std::string::npos)
        return "";
    std::string query = requestTarget.substr(query_pos + 1);
    requestTarget.erase(query_pos);
    return query;
}

void RequestParser::parseRequestLine(Request &request)
{
    size_t first_space = rawRequestLine_.find(' ');
    if (first_space == std::string::npos)
    {
        throw 400;
    }
    std::string method = rawRequestLine_.substr(0, first_space);
    size_t second_space = rawRequestLine_.find(' ', first_space + 1);
    if (second_space == std::string::npos)
    {
        throw 400;
    }
    std::string requestTarget = rawRequestLine_.substr(first_space + 1, second_space - (first_space + 1));
    if (!validateRequestTarget(requestTarget))
    {
        throw 400;
    }
    std::string version = rawRequestLine_.substr(second_space + 1);
    std::string query = parseQuery(requestTarget);
    validateRequestLine(method, requestTarget, version);
    request.setMethod(method);
    request.setPath(requestTarget);
    request.setQuery(query);
    request.setVersion(version);
}

void RequestParser::parseHeaderLine(const std::string &line, std::map<std::string, std::string> &headers)
{
    size_t delim = line.find(':');
    if (delim == std::string::npos)
    {
        throw 400;
    }
    std::string key = line.substr(0, delim);
    if (key.empty() || key.find(' ') != std::string::npos || key.find('\t') != std::string::npos 
        || key.find('\r') != std::string::npos || key.find('\n') != std::string::npos)
    {
        throw 400;
    }
    key = toLower(key);
    size_t value_start = delim + 1;
    while (value_start < line.size() && (line[value_start] == ' '
                                            || line[value_start] == '\t'))
        value_start++;
    size_t value_end = line.size();
    while (value_end > value_start && (line[value_end - 1] == ' ' || line[value_end - 1] == '\t'))
        value_end--;
    std::string value = line.substr(value_start, value_end - value_start);
    if (headers.find(key) != headers.end())
    {
        throw 400;
    }
    headers[key] = value;
}

void RequestParser::parseHeader(Request &request)
{
    std::map<std::string, std::string> headers;
    size_t i = 0;
    while (i < rawHeaders_.size())
    {
        size_t line_end = rawHeaders_.find("\r\n", i);
        if (line_end == std::string::npos)
            line_end = rawHeaders_.size();
        parseHeaderLine(rawHeaders_.substr(i, line_end - i), headers);
        i = line_end + 2;
    }
    if (request.getVersion() == "HTTP/1.1" && headers.find("host") == headers.end())
    {
        throw 400;
    }
    request.setHeaders(headers);
}

void RequestParser::parseRequest(const std::string &raw_request, Request &request)
{
    endOfHeaders_ = 0;
    size_t headersEnd = raw_request.find("\r\n\r\n");
    if (headersEnd == std::string::npos)
    {
        if (raw_request.size() > MAX_HEADER_SIZE)
            throw 431;
        status_ = PARSE_INCOMPLETE;
        return ;
    }
    size_t requestLineEnd = raw_request.find("\r\n");
    rawRequestLine_ = raw_request.substr(0, requestLineEnd);

    size_t headersStart = requestLineEnd + 2;
    if (requestLineEnd == headersEnd)
        rawHeaders_ = "";
    else
        rawHeaders_ = raw_request.substr(headersStart, headersEnd - headersStart);
    parseRequestLine(request);
    parseHeader(request);
    endOfHeaders_ = headersEnd + 4;
    status_ = PARSE_OK;
}

int RequestParser::copyByLength(std::map<std::string, std::string> header, Request &request)
{
    std::map<std::string, std::string>::iterator it = header.find("content-length");
    if (it == header.end())
        return (-1);
    char *end;
    long value = std::strtol(it->second.c_str(), &end, 10);
    if (it->second.empty() || *end != '\0' || value < 0)
        throw 400;
    size_t n = static_cast<size_t>(value);
    size_t missing = n - request.getBody().size();
    std::string newBody = rawRequestLine_.substr(0, missing);
    request.appendToBody(newBody);
    /* size_t bodyEnd = rawRequestLine_.find("\r\n\r\n");
    if (bodyEnd == std::string::npos)
        return (-1); */
    std::string body = request.getBody();
    endOfBody_ = newBody.size();
    if (body.size() != n)
    {
        if (body.size() < n)
        {
            request.setIsBodyComplete(false);
            return (-1);
        }
        else
        {
            std::cout << "I don't know what we do here yet, because "
                "that's just weird behaviour. But I think we should throw." << std::endl;
            return (-1);
        }
    }
    else
        request.setIsBodyComplete(true);
    return (0);
}


/**
 * The idea behind both -1 returns is to wait for the complete message,
 * because it's most likely incomplete so we should wait for the rest of the input.
 */
int RequestParser::getChunkIndex(size_t &endline, std::string &hex, size_t &n, std::string &str, char *end)
{
    endline = str.find("\r\n");
    if (endline == std::string::npos)
    {
        std::cout << "There's no \\r\\n yet." << std::endl;
        return (-1);
    }
    hex = str.substr(0, endline);
    long value = std::strtol(hex.c_str(), &end, 16);
    if (hex.empty() || value < 0 || value == LONG_MAX)
        throw 400;
    n = static_cast<size_t>(value);
    if ((*end != '\0' && *end != ';'))
        throw 400;
    if (str.size() < endline + 2 + n + 2)
    {
        std::cout << "The expected size is bigger than the actual size, "
            "which means the message is incomplete." << std::endl;
        return (-1);
    }
    if (str.compare(endline + 2 + n, 2, "\r\n") != 0)
        throw 400;
    return (0);
}

int RequestParser::copyByChunks(std::map<std::string, std::string> header, Request &request)
{
    std::map<std::string, std::string>::iterator it = header.find("transfer-encoding");
    std::string str = rawRequestLine_;
    if (it == header.end())
        return (-1);
    if (toLower(it->second) != "chunked")
        throw 501;
    char *end = NULL;
    size_t endline;
    std::string hex;
    size_t n;
    std::cout << it->first << ": " << it->second << std::endl;
    if (getChunkIndex(endline, hex, n, str, end) == -1)
        return (-1);
    while (n != 0)
    {
        endOfBody_ += str.find("\r\n") + 2;
        str.erase(0, str.find("\r\n") + 2);
        request.appendToBody(str.substr(0, n));
        endOfBody_ += n + 2;
        str.erase(0, n + 2);
        if (getChunkIndex(endline, hex, n, str, end) == -1)
            return (-1);
    }
    if (str.find("0\r\n\r\n") != 0)
        throw 400;
    endOfBody_ += str.find("\r\n\r\n") + 4;
    str.erase(0, str.find("\r\n\r\n") + 4);
    request.setIsBodyComplete(true);
    return (0);
}

/**
 * TODO: Hello World body test
 * * content-length -> (printf 'POST /upload HTTP/1.1\r\nHost: x\r\nContent-Length: 11\r\n\r\nhello world'; sleep 1) | nc 127.0.0.1 8080
 * * transfer-encoding -> (printf 'POST /upload HTTP/1.1\r\nHost: x\r\nTransfer-encoding: chunked\r\n\r\n6\r\nhello \r\n5\r\nworld\r\n0\r\n\r\n'; sleep 1) | nc 127.0.0.1 8080
 * * content-length + transfer-encoding -> (printf 'POST /upload HTTP/1.1\r\nHost: x\r\nContent-Length: 11\r\nTransfer-encoding: chunked\r\n\r\n6\r\nhello \r\n6\r\nworld\r\n0\r\n\r\n'; sleep 1) | nc 127.0.0.1 8080
 */
int RequestParser::parseRequestBody(const std::string &raw_request, Request &request)
{
    endOfBody_ = 0;
    rawRequestLine_ = raw_request;
    std::map<std::string, std::string> header = request.getHeaders();
    std::map<std::string, std::string>::iterator chunked = header.find("transfer-encoding");
    std::map<std::string, std::string>::iterator lenght = header.find("content-length");

    if (chunked != header.end() && lenght != header.end())   // ← both present
        throw 400;
    if (chunked != header.end())
        return (copyByChunks(header, request));
    if (lenght != header.end())
        return (copyByLength(header, request));
    request.setIsBodyComplete(true);
    return (0);
}
