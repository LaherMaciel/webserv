#include "RequestParser.hpp"
#include "webserv.hpp"
#include "Request.hpp"
#include <cstdlib>

RequestParser::RequestParser() : status_(PARSE_INCOMPLETE), endOfHeaders_(0) {}
RequestParser::~RequestParser() {}

void RequestParser::validateRequestLine(const std::string &method, const std::string &path, const std::string &version)
{
    if (method != "GET" && method != "POST" && method != "DELETE")//add all possible methods with http 1.1
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
    std::string path = rawRequestLine_.substr(first_space + 1, second_space - (first_space + 1));
    std::string version = rawRequestLine_.substr(second_space + 1);
    validateRequestLine(method, path, version);
    request.setMethod(method);
    request.setPath(path);
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
    std::string value = line.substr(value_start);
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
    size_t headersEnd = raw_request.find("\r\n\r\n");
    if (headersEnd == std::string::npos)
        return ;
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

/**
 * (printf 'POST /upload HTTP/1.1\r\nHost: x\r\nContent-Length: 11\r\n\r\nhello world'; sleep 1) | nc 127.0.0.1 8080
 */
int RequestParser::copyByLength(std::map<std::string, std::string> header, const std::string &raw_request, Request &request)
{
    std::map<std::string, std::string>::iterator it = header.find("content-length");
    if (it == header.end())
        return (-1);
    size_t n = std::atoi(it->second.c_str());
    request.appendToBody(raw_request.substr(0, n));
    /* size_t bodyEnd = raw_request.find("\r\n\r\n");
    if (bodyEnd == std::string::npos)
        return (-1); */
    std::string body = request.getBody();
    if (body.size() != n)
    {
        if (body.size() < n)
        {
            request.setIsBodyComplete(false);
            return (-1);
        }
        else
        {
            std::cout << "I don't know what we do here yet because "
                "that's just weird behavior. But I think we should throw." << std::endl;
            return (-1);
        }
    }
    else
        request.setIsBodyComplete(true);
    std::cout << "Content-Length BODY: " << request.getBody() << std::endl;
    return (0);
}

/**
 * (printf 'POST /upload HTTP/1.1\r\nHost: x\r\nTransfer-encoding: chunk\r\n\r\n6\r\nhello \r\n5\r\nworld'; sleep 1) | nc 127.0.0.1 8080
 */
int RequestParser::copyByChunks(std::map<std::string, std::string> header, const std::string &raw_request, Request &request)
{
    std::map<std::string, std::string>::iterator it = header.find("transfer-encoding");
    std::string str = raw_request;
    if (it == header.end())
        return (-1);
    std::cout << it->first << ": " << it->second << std::endl;
    std::string hex = str.substr(0, str.find("\r\n"));
    char *end;
    size_t n = std::strtol(hex.c_str(), &end, 16);
    while (n != 0)
    {
        str.erase(0, str.find("\r\n") + 2);
        request.appendToBody(str.substr(0, n));
        str.erase(0, n + 2);
        hex = str.substr(0, str.find("\r\n"));
        n = std::strtol(hex.c_str(), &end, 16);
    }
    str.erase(0, str.find("\r\n\r\n") + 4);
    std::cout << "Transfer-Encoding BODY: " << request.getBody() << std::endl;
    return (0);
}

/**
 * (printf 'POST /upload HTTP/1.1\r\nHost: x\r\nContent-Length: 11\r\nTransfer-encoding: chunk\r\n\r\n6\r\nhello \r\n6\r\nworld'; sleep 1) | nc 127.0.0.1 8080
 */
int RequestParser::parseRequestBody(const std::string &raw_request, Request &request)
{
    std::map<std::string, std::string> header = request.getHeaders();
    std::cout << "INSIDE BODY" << std::endl << raw_request << std::endl;
    if (copyByChunks(header, raw_request, request) == 0
        || copyByLength(header, raw_request, request) == 0)
    {
        std::cout << "BODY: " << request.getBody() << std::endl;
        return (0);
    }
    
    return (0);
}
