#ifndef REQUESTPARSER_HPP
# define REQUESTPARSER_HPP

#include <iostream>
#include <string>
#include <map>

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
        ParseStatus parseRequest(const std::string &raw_request);
        ParseStatus parseRequestLine();
        ParseStatus parseHeader();
        int getErrorCode() const;
	private:
        std::string method_;
        std::string path_;
        std::string version_;
        std::map<std::string, std::string> headers_;

        int errorCode_;
        std::string rawRequestLine_;
        std::string rawHeaders_;
        RequestParser(const RequestParser& other);
        RequestParser& operator=(const RequestParser& other);

};

#endif