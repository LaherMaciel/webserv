/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Request.cpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lahermaciel <lahermaciel@student.42.fr>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/09/08 03:03:48 by lahermaciel       #+#    #+#             */
/*   Updated: 2026/09/10 17:02:51 by lahermaciel      ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Connection.hpp"
#include "webserv.hpp"
#include "Response.hpp"

Request initStruct()
{
    Request request;
    request.method = "";
    request.url = "";;
    request.version = "";;
    request.body = "";;
    request.bufferSize = 0;
    request.code = 200;
    return (request);
}

void    Connection::requestError(int code, Request &request)
{
    request.code = code;
    throw std::runtime_error(statusText(code));
}

void    Connection::ParseMethod(Request &request, std::string startLine, int pos)
{
    request.method = startLine.substr(0, pos);
    if (request.method.empty())
        requestError(400, request);
    if (request.method != "GET" && request.method != "POST"
        && request.method != "DELETE")
        requestError(501, request);
}

void Connection::ParseUrl(Request &request, std::string startLine, int pos)
{
    request.url = startLine.substr(0, pos);
    if (request.url.empty() || request.url[0] != '/')
        requestError(400, request);
    if (request.url.size() > MAX_URL_SIZE)
        requestError(414, request);
}

void    Connection::ParseStartLine(Request &request)
{
    std::string startLine;
    size_t      i = 0;
    size_t      pos;

    request.bufferSize = in_buffer.size();
    pos = in_buffer.find("\r\n");
    if (pos == std::string::npos)
        requestError(400, request);
    startLine = in_buffer.substr(0, pos);
    while (i < 3)
    {
        if (i < 2)
           pos = startLine.find(" ");
        else
            pos = startLine.size();
        if (pos == std::string::npos)
            requestError(400, request);
        switch (i)
        {
            case 0:
                ParseMethod(request, startLine, pos);
                break ;
            case 1:
                ParseUrl(request, startLine, pos);
                break ;
            case 2:
                request.version = startLine.substr(0, pos);
                break ;
        }
        startLine = startLine.erase(0, pos + 1);
        i++;
    }
    in_buffer = in_buffer.erase(0, in_buffer.find("\r\n") + 2);
}

void    Connection::ParseHeader(Request &request)
{
    std::string header;
    std::string key;
    size_t      pos;
    size_t      linelen;

    linelen = in_buffer.find("\r\n\r\n");
    if (linelen == std::string::npos)
        requestError(400, request);
    header = in_buffer.substr(0, linelen + 2);
    while (header.size() > 0)
    {
        linelen = header.find("\r\n");
        if (linelen == std::string::npos)
            requestError(400, request);
        if (linelen == 0)
            break ;
        std::string line = header.substr(0, linelen);
        pos = line.find(": ");
        if (pos > linelen)
            requestError(400, request);
        key = line.substr(0, pos);
        request.header[key] = line.substr(pos + 2, linelen - (pos + 2));
        header = header.erase(0, linelen + 2);
    }
    in_buffer = in_buffer.erase(0, in_buffer.find("\r\n\r\n") + 4);
}

/**
 * I still have to create this function. I'm thinking of creating a variable in
 * the request struct where, right at the start before doing anything to the
 * in_buffer, I check its size. Then I do all the work that I have to do,
 * and at the end I check if there's a Content-Length variable in the header map,
 * and if the size stated there matches the size I have in the request variable.
 * If the content-length is bigger than the actual length I received, then I know
 * there's more information to receive, and I call this function to fill the body.
 * Or something like that. I still have to think more about it.
 */
Request    Connection::ParseBody(Request &request)
{
    return (request);
}

/**
 * TODO: For now I'm working in the url parsing. need to do a Query string to
 * TODO: read properly the URL /search?q=cat in to path /search find q=cat or
 * TODO: something like that, I need to search more about that
 *
 * TODO: OH DAMN, IM READYING HERE ABOUT THE URL CHECKS... THIS WILL THAT SOME TIME....
 *
 * * Things to do:
 * * Create the method to know if we received the full message
 * *    (for example if the content-length that was meant to be sent is 1500 and we
 * *    received 1000, it means we have to way for the rest of the information before
 * *    actually send the reply) 
 * * Parse Header
 * * Parse the body
 * ? what am I missing in the to do list?
 */
void    Connection::RequestParsing(Request &request)
{
    ParseStartLine(request);
    ParseHeader(request);
    ParseBody(request);
    std::cout << "END RECEIVED OF REQUEST" << std::endl << std::endl << std::endl;
}
