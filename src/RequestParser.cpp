#include "RequestParser.hpp"
#include "webserv.hpp"
#include "Request.hpp"

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
