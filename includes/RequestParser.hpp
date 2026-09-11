#ifndef REQUESTPARSER_HPP
# define REQUESTPARSER_HPP

#include <iostream>
#include <string>
#include <map>

class Request;

enum ParseStatus
{
    PARSE_INCOMPLETE,
    PARSE_OK,
    PARSE_ERROR
};

class RequestParser
{
	public:
		RequestParser();
		~RequestParser();
        ParseStatus parseRequest(const std::string &raw_request, Request &request);
        ParseStatus parseRequestLine(Request &request);
        ParseStatus validateRequestLine(const std::string &method, const std::string &path, const std::string &version);
        ParseStatus parseHeader(Request &request);
        ParseStatus parseHeaderLine(const std::string &line, std::map<std::string, std::string> &headers);
        int getErrorCode() const;
    
    private:
        int errorCode_;
        std::string rawRequestLine_;
        std::string rawHeaders_;
        RequestParser(const RequestParser& other);
        RequestParser& operator=(const RequestParser& other);

};

#endif