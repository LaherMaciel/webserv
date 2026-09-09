#include "RequestParser.hpp"
#include "webserv.hpp"

RequestParser::RequestParser() : method_(""), path_(""), version_(""), headers_(), errorCode_(0) {}
RequestParser::~RequestParser() {}

int RequestParser::getErrorCode() const
{
    return errorCode_;
}

ParseStatus RequestParser::validateRequestLine()
{
    if (method_ != "GET" && method_ != "POST" && method_ != "DELETE")
    {
        errorCode_ = 400;
        return PARSE_ERROR;
    }
    if (path_.empty() || path_[0] != '/')
    {
        errorCode_ = 400;
        return PARSE_ERROR;
    }
    if (version_ != "HTTP/1.1" && version_ != "HTTP/1.0")
    {
        errorCode_ = 400;
        return PARSE_ERROR;
    }
    return PARSE_OK;
}

ParseStatus RequestParser::parseRequestLine()
{
    size_t first_space = rawRequestLine_.find(' ');
    if (first_space == std::string::npos)
    {
        errorCode_ = 400;
        return PARSE_ERROR;
    }
    method_ = rawRequestLine_.substr(0, first_space);
    size_t second_space = rawRequestLine_.find(' ', first_space + 1);
    if (second_space == std::string::npos)
    {
        errorCode_ = 400;
        return PARSE_ERROR;
    }
    path_ = rawRequestLine_.substr(first_space + 1, second_space - (first_space + 1));
    version_ = rawRequestLine_.substr(second_space + 1);
    return validateRequestLine();
}

ParseStatus RequestParser::parseHeaderLine(const std::string &line)
{
    size_t delim = line.find(':');
    if (delim == std::string::npos)
    {
        errorCode_ = 400;
        return PARSE_ERROR;
    }
    std::string key = line.substr(0, delim);
    if (key.empty() || key.find(' ') != std::string::npos || key.find('\t') != std::string::npos 
        || key.find('\r') != std::string::npos || key.find('\n') != std::string::npos)
    {
        errorCode_ = 400;
        return PARSE_ERROR;
    }
    key = toLower(key);
    size_t value_start = delim + 1;
    while (value_start < line.size() && (line[value_start] == ' '
                                            || line[value_start] == '\t'))
        value_start++;
    std::string value = line.substr(value_start);
    headers_.insert(std::make_pair(key, value));
    std::cout << "Parsed header: [" << key << "]: [" << value << "]" << std::endl;
    return PARSE_OK;
}

ParseStatus RequestParser::parseHeader()
{
    size_t i = 0;
    while (i < rawHeaders_.size())
    {
        size_t line_end = rawHeaders_.find("\r\n", i);
        if (line_end == std::string::npos)
            line_end = rawHeaders_.size();
        ParseStatus status = parseHeaderLine(rawHeaders_.substr(i, line_end - i));
        if (status != PARSE_OK)
            return status;
        i = line_end + 2;
    }
    return PARSE_OK;
}

ParseStatus RequestParser::parseRequest(const std::string &raw_request)
{
    size_t header_end_pos = raw_request.find("\r\n\r\n");
    if (header_end_pos == std::string::npos)
        return PARSE_INCOMPLETE;

    size_t request_line_end_pos = raw_request.find("\r\n");
    rawRequestLine_ = raw_request.substr(0, request_line_end_pos);
    size_t headers_start = request_line_end_pos + 2;
    rawHeaders_ = raw_request.substr(headers_start, header_end_pos - headers_start);
    ParseStatus status = parseRequestLine();
    if (status != PARSE_OK)
        return status;
    status = parseHeader();
    if (status != PARSE_OK)
        return status;
    if (version_ == "HTTP/1.1" && headers_.find("host") == headers_.end())
    {
        errorCode_ = 400;
        return PARSE_ERROR;
    }
    return PARSE_OK;
}
