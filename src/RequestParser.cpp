#include "RequestParser.hpp"

RequestParser::RequestParser() : method_(""), path_(""), version_(""), headers_(), errorCode_(0) {}
RequestParser::~RequestParser() {}

int RequestParser::getErrorCode() const
{
    return errorCode_;
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
    if (version_ != "HTTP/1.1" && version_ != "HTTP/1.0")
    {
        errorCode_ = 505;
        return PARSE_ERROR;
    }
    return PARSE_OK;
}

ParseStatus RequestParser::parseHeader()
{
    size_t i = 0;
    while (i < rawHeaders_.size())
    {
        size_t delim = rawHeaders_.find(": ", i);
        if (delim == std::string::npos)
        {
            if (rawHeaders_.substr(i) == "\r\n")
                break;
            errorCode_ = 400;
            return PARSE_ERROR;
        }
        std::string key = rawHeaders_.substr(i, delim - i);
        size_t value_start = delim + 2;
        size_t value_end = rawHeaders_.find("\r\n", value_start);
        if (value_end == std::string::npos)
            value_end = rawHeaders_.size();
        std::string value = rawHeaders_.substr(value_start, value_end - value_start);
        headers_.insert(std::make_pair(key, value));
        i = value_end + 2;
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
    std::cout << "Raw request line: " << rawRequestLine_ << std::endl;
    size_t headers_start = request_line_end_pos + 2;
    rawHeaders_ = raw_request.substr(headers_start, header_end_pos - headers_start);
    std::cout << "Raw headers: " << rawHeaders_ << std::endl;
    ParseStatus status = parseRequestLine();
    if (status != PARSE_OK)
        return status;
    status = parseHeader();
    if (status != PARSE_OK)
        return status;
    return PARSE_OK;
}