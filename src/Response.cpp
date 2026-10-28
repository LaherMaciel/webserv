#include "Response.hpp"
#include "webserv.hpp"

Response::Response() : statusCode_(500), version_("HTTP/1.1"), body_("")
{
    setHeader("Content-Length", "0");
    reasonPhrase_ = httpReasonPhrase(statusCode_);
}

Response::Response(int statusCode, const std::string &version)
    : statusCode_(statusCode), version_(version)
{
    setHeader("Content-Length", "0");
    reasonPhrase_ = httpReasonPhrase(statusCode);
}

Response::Response(int statusCode, const std::string &version, const std::string &body, const std::string &contentType)
    : statusCode_(statusCode), version_(version), body_(body)
{
    setHeader("Content-Type", contentType);
    setHeader("Content-Length", toString(body.size()));
    reasonPhrase_ = httpReasonPhrase(statusCode);
}

Response::~Response() {}

Response::Response(const Response& other)
    : statusCode_(other.statusCode_), reasonPhrase_(other.reasonPhrase_)
    , version_(other.version_), body_(other.body_)
    , headers_(other.headers_), errorPages_(other.errorPages_) {}

Response& Response::operator=(const Response& other)
{
    if (this != &other)
    {
        statusCode_ = other.statusCode_;
        reasonPhrase_ = other.reasonPhrase_;
        version_ = other.version_;
        body_ = other.body_;
        headers_ = other.headers_;
        errorPages_ = other.errorPages_;
    }
    return *this;
}

void Response::setBody(const std::string& body, const std::string& contentType)
{
    body_ = body;
    if (headers_.find("Content-Type") == headers_.end())
        setHeader("Content-Type", contentType);
    setHeader("Content-Length", toString(body.size()));
}

void Response::setHeader(const std::string& key, const std::string& value) { headers_[key] = value; }

void Response::setHeaders(const std::map<std::string, std::string>& headers)
{
    for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it)
        headers_[it->first] = it->second;
}

void Response::setStatusCode(int statusCode)
{
    statusCode_ = statusCode;
    reasonPhrase_ = httpReasonPhrase(statusCode);
}

void Response::setStatusCode(int statusCode, const std::string& reasonPhrase)
{
    statusCode_ = statusCode;
    reasonPhrase_ = reasonPhrase;
}

void Response::setErrorPage(const std::map<int, std::string>&  errorPages) {errorPages_ = errorPages;}

bool    Response::errorPage()
{
    std::map<int, std::string>::const_iterator it = errorPages_.find(statusCode_);
    if (it == errorPages_.end())
        return (false);
    std::string contType = contentType(it->second);
    std::string body;
    if (!readFile(it->second, body))
    {
        std::cerr << "error_page: cannot read " << quoteValue(it->second) << std::endl;
        return (false);
    }
    setHeader("Content-Type", contType);
    setBody(body);
    return (true);
}

void Response::setDefaultErrorBody()
{
    if (errorPage())
        return ;
    std::string body;
    body = "<!DOCTYPE html>"
            "<html>"
            "<head>"
            "<title>Error</title>"
            "<style>"
            "html { color-scheme: light dark; }"
            "body { width: 35em; margin: 0 auto;"
            "font-family: Tahoma, Verdana, Arial, sans-serif; }"
            "</style>"
            "</head>"
            "<body>"
            "<h1>An error occurred.</h1>"
            "<p>Sorry, the page you are looking for is currently unavailable.<br/>"
            "Please try again later.</p>"
            "<p>If you are the system administrator of this resource then you should check"
            "the error log for details.</p>"
            "<p><em>Faithfully yours, nginx.</em></p>"
            "</body>"
            "</html>";

    setHeader("Content-Type", "text/html");
    setBody(body);
}

std::string Response::serialize() const
{
    //1) HTTP-Version SP Status-Code SP Reason-Phrase \r\n
    std::string response = version_ + " " + toString(statusCode_) + " " + reasonPhrase_ + "\r\n";
    //2) Headers
    for (std::map<std::string, std::string>::const_iterator it = headers_.begin(); it != headers_.end(); ++it)
        response += it->first + ": " + it->second + "\r\n";
    //3) Blank line
    response += "\r\n";
    //4) Body (if any)
    response += body_;
    return response;
}
