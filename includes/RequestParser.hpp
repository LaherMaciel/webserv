#ifndef REQUESTPARSER_HPP
# define REQUESTPARSER_HPP

# include <iostream>
# include <string>
# include <map>

# define MAX_HEADER_SIZE 8192

class Request;

enum ParseStatus
{
    PARSE_INCOMPLETE,
    PARSE_OK,
};

class RequestParser
{
    public:
        size_t  endOfHeaders_;
        size_t  endOfBody_;
        ParseStatus status_;

        RequestParser();
        ~RequestParser();

        int     parseRequestBody(const std::string &raw_request, Request &request);
        void    parseRequest(const std::string &raw_request, Request &request);
        void    parseHeader(Request &request);

    private:
        std::string rawRequestLine_;
        std::string rawHeaders_;

        RequestParser(const RequestParser& other);
        RequestParser& operator=(const RequestParser& other);

        int     getChunkIndex(size_t &endline, std::string &hex, size_t &n, std::string &str, char *end);
        int     copyByLength(std::map<std::string, std::string> header, Request &request);
        int     copyByChunks(std::map<std::string, std::string> header, Request &request);
        void    parseRequestLine(Request &request);
        void    validateRequestLine(const std::string &method, const std::string &path, const std::string &version);
        void    parseHeaderLine(const std::string &line, std::map<std::string, std::string> &headers);
};

#endif