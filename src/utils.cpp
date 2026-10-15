#include "webserv.hpp"
#include <string>
#include <cctype> // for std::tolower
#include <sstream> // for std::ostringstream
#include <fstream> // for std::ifstream
#include <sys/stat.h>//for stat(), S_ISREG
#include <fcntl.h>//for fcntl()

//Note: commented out version superior but not allowed by project subject 
int set_non_blocking(int fd)
{
    // int flags = fcntl(fd, F_GETFL, 0);
    // if (flags == -1)
    //     return -1;
    // if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    //     return -1;
    if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1)
        return -1;
    return 0;
}

std::string toLower(std::string s)
{
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
    return s;
}

std::string toString(size_t value)
{
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

std::string quoteValue(const std::string &s)
{
    std::string out = "'";

    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\t')
            out += "\\t";
        else if (s[i] == '\n')
            out += "\\n";
        else if (s[i] == '\r')
            out += "\\r";
        else if (s[i] < 32 || s[i] > 126)
            out += "\\?";
        else
            out += s[i];
    }
    out += "'";
    return (out);
}

bool readFile(const std::string& path, std::string& content)
{
    struct stat sb;

    if (stat(path.c_str(), &sb) != 0 || !S_ISREG(sb.st_mode))
        return false;

    std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
    if (!file)
        return false;

    std::ostringstream ss;
    ss << file.rdbuf();
    content = ss.str();
    return true;
}

/**
 * Returns the index of the first non-whitespace character at or after 'pos'.
 *
 * When everything from 'pos' onward is whitespace it returns str.size(), so
 * the caller's "pos == str.size()" check still means "nothing left here".
 * It never returns an index that cannot be read.
 */
size_t skipSpaces(const std::string &str, size_t pos)
{
    pos = str.find_first_not_of(WHITESPACE, pos);
    if (pos == std::string::npos)
        return (str.size());
    return (pos);
}

/**
 * Removes trailing whitespace from 'str', in place.
 */
void removeSpaceAfter(std::string &str)
{
    size_t last = str.find_last_not_of(WHITESPACE);

    if (last == std::string::npos)
        str.clear();
    else
        str.erase(last + 1);
}

/*
418 I'm a teapot

    The server refuses the attempt to brew coffee with a teapot.
    429 Too Many Requests

    The user has sent too many requests in a given amount of time (rate limiting).
502 Bad Gateway

    This error response means that the server, while working as a gateway to get a response needed to handle the request, got an invalid response.

*/
std::string httpReasonPhrase(int code)
{
    switch (code)
    {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 418: return "I'm a teapot";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "502 Bad Gateway";
        case 505: return "HTTP Version Not Supported";
        default:  return "Internal Server Error";
    }
}
