/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Request.cpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: lahermaciel <lahermaciel@student.42.fr>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/09/08 03:03:48 by lahermaciel       #+#    #+#             */
/*   Updated: 2026/09/08 03:45:38 by lahermaciel      ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Connection.hpp"

Request    Connection::ParseRequestLine()
{
    Request     request;
    std::string requestLine;
    size_t      i = 0;
    size_t      pos;

	request.bufferSize = in_buffer.size();
    pos = in_buffer.find("\r\n");
	if (pos == std::string::npos)
		throw std::runtime_error("400");
    requestLine = in_buffer.substr(0, pos);
    while (i < 3)
    {
        if (i < 2)
           pos = requestLine.find(" ");
        else
            pos = requestLine.size();
        if (pos == std::string::npos)
            throw std::runtime_error("400");
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
            throw std::runtime_error("400");
    header = in_buffer.substr(0, linelen + 2);
    while (header.size() > 0)
    {
        linelen = header.find("\r\n");
        if (linelen == std::string::npos)
            throw std::runtime_error("400");
		if (linelen == 0)
			break ;
        std::string line = header.substr(0, linelen);
        pos = line.find(": ");
        if (pos > linelen)
            throw std::runtime_error("400");
        key = line.substr(0, pos);
        request.header[key] = line.substr(pos + 2, linelen - (pos + 2));
        header = header.erase(0, linelen + 2);
    }
    in_buffer = in_buffer.erase(0, in_buffer.find("\r\n\r\n") + 4);
    return (request);
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

/**
 * This isn't parsing for now. I'm just receiving the information and storing it
 * as it goes. I don't check much of it for now and I don't give any errors for
 * now. Ah, and I still have to make proper error messages with the correct
 * status codes. I still have to do a deep dive on that part.
 *
 * So for now it just receives the in_buffer, separates the information - the
 * method, the url, the version, headers, the body - in a really basic, almost
 * raw way, while also cleaning the in_buffer. So if everything goes well, then
 * the request should have all the information already organized and ready to
 * use, and the in_buffer should be empty. And we should also be able to know if
 * theres information missing. or not.
 * 
 * Again, all the throws in this file are temporary.
 */
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
