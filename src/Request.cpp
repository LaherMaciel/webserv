/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Request.cpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lahermaciel <lahermaciel@student.42.fr>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/09/08 03:03:48 by lahermaciel       #+#    #+#             */
/*   Updated: 2026/09/08 03:05:39 by lahermaciel      ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Connection.hpp"

Request    Connection::ParseRequestLine()
{
    Request     request;
    std::string requestLine;
    size_t      i = 0;
    size_t      pos;

    pos = in_buffer.find("\r\n");
    requestLine = in_buffer.substr(0, pos);
    while (i < 3)
    {
        if (i < 2)
           pos = requestLine.find(" ");
        else
            pos = requestLine.size();
        if (pos == std::string::npos)
            throw ;
        switch (i)
        {
            case 0:
                request.method = requestLine.substr(0, pos);
                break ;
            case 1:
                request.url = requestLine.substr(0, pos);
                break ;
            case 2:
                request.version = requestLine.substr(0, pos);
                break ;
        }
        requestLine = requestLine.erase(0, pos + 1);
        i++;
    }
    in_buffer = in_buffer.erase(0, in_buffer.find("\r\n") + 2);
    return (request);
}

Request    Connection::ParseHeader(Request request)
{
    std::string header;
    std::string key;
    size_t      pos;
    size_t      linelen;

    linelen = in_buffer.find("\r\n\r\n");
    if (linelen == std::string::npos)
            throw ;
    header = in_buffer.substr(0, linelen + 2);
    while (header.size() > 0)
    {
        linelen = header.find("\r\n");
        if (linelen == std::string::npos)
            throw ;
        std::string line = header.substr(0, linelen);
        pos = line.find(": ");
        if (pos > linelen)
            throw ;
        key = line.substr(0, pos);
        request.header[key] = line.substr(pos + 2, linelen - (pos + 2));
        header = header.erase(0, linelen + 2);
    }
    in_buffer = in_buffer.erase(0, in_buffer.find("\r\n\r\n") + 4);
    return (request);
}

Request    Connection::ParseBody(Request request)
{
    return (request);
}

static void    printRequest(Request request)
{
    std::map<std::string, std::string>::const_iterator it;

    std::cout << std::endl << std::endl;
    std::cout << "METHOD: " << request.method << "  URL: " << request.url << "  VERSION: " << request.version << std::endl;
    std::cout << "HEADER: " << std::endl;
    for (it = request.header.begin(); it != request.header.end(); ++it)
    {
        std::cout << "KEY: " << it->first << "  |  BODY: " << it->second << std::endl;
    }
    std::cout << "HEADER BODY:  " << request.body << std::endl;
}

int    Connection::RequestParsing()
{
    Request request;

    try
    {
        request = ParseRequestLine();
        request = ParseHeader(request);
        request = ParseBody(request);
        printRequest(request);
        std::cout << "END RECEIVED OF REQUEST" << std::endl << std::endl << std::endl;
    }
    catch(std::exception& e)
    {
        std::cerr << e.what() << '\n';
        sendResponse(400);
        return (-1);
    }
    return (0);
}
