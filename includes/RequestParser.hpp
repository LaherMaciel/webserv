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
};

class RequestParser
{
	public:
		RequestParser();
		~RequestParser();
        void parseRequest(const std::string &raw_request, Request &request);
        void parseRequestLine(Request &request);
        void validateRequestLine(const std::string &method, const std::string &path, const std::string &version);
        void parseHeader(Request &request);
        void parseHeaderLine(const std::string &line, std::map<std::string, std::string> &headers);
        ParseStatus status_;
        size_t endOfHeaders_;
        
        private:
        std::string rawRequestLine_;
        std::string rawHeaders_;
        std::string parseQuery(std::string &requestTarget);
        bool validateRequestTarget(const std::string &target);
        RequestParser(const RequestParser& other);
        RequestParser& operator=(const RequestParser& other);

};

#endif