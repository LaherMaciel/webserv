/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   cpp                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lahermaciel <lahermaciel@student.42.fr>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/09/08 15:13:07 by lahermaciel       #+#    #+#             */
/*   Updated: 2026/09/08 18:18:43 by lahermaciel      ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Connection.hpp"
#include "Response.hpp"
#include "webserv.hpp"

Response::Response(): version("HTTP/1.1"), code(200), body("") {}

Response::Response(std::string v, int cd): version(v), code(cd), body(""){}

Response::~Response(){}

Response::Response(const Response& other)
{
    header = other.header;
    version = other.version;
    code = other.code;
    body = other.body;
}

Response& Response::operator=(const Response& other)
{
    if (this != &other)
    {
        header = other.header;
        version = other.version;
        code = other.code;
        body = other.body;
    }
    return (*this);
}

void    Response::buildBody(Request request)
{
    std::map<std::string, std::string>::const_iterator it;

    std::string rawBody;
    rawBody = "METHOD: ";
    rawBody += request.method ;
    rawBody += "  URL: ";
    rawBody += request.url;
    rawBody += "  VERSION: ";
    rawBody += request.version;
    rawBody += "\r\n\r\n";

    rawBody += "HEADER: \n";
    for (it = request.header.begin(); it != request.header.end(); ++it)
    {
        rawBody += it->first + ": ";
        rawBody += it->second;
        rawBody += "\r\n";
    }
    rawBody += "\r\n";

    rawBody += "HEADER BODY: \n" + request.body + "\r\n\r\n";
    body = rawBody;
}

void	Response::buildHeader()
{
    header["Server"] = "webserv";
    header["Date"] = "Today (Yeah I need to build the date thing)";
    if (body.size() != 0)
    {
        header["Content-Length"] = toString(body.size());
        header["Content-Type"] = "text/plain";
    }
    header["Cache-Control"] = "no-store";
}

void Response::buildResponse(Request request)
{
    code = request.code;
    version = request.version;
    buildBody(request);
    buildHeader();
}

std::string Response::getResponse()
{
    std::string page;

    page = version + " " + statusText(code);

    std::map<std::string, std::string>::const_iterator it;
    for (it = header.begin(); it != header.end(); ++it)
    {
        page += it->first + ": " + it->second + "\r\n";
    }
    page += "\r\n";
    page += body;
    return (page);
}

