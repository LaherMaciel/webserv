#include "ServerConfig.hpp"
#include "webserv.hpp"
#include <stdexcept>

void stripComments(std::string &configFile)
{
    std::string out;
    size_t      pos = 0;

    out.reserve(configFile.size());
    while (pos < configFile.size())
    {
        size_t hash = configFile.find('#', pos);

        if (hash == std::string::npos)
        {
            out.append(configFile, pos, configFile.size() - pos);
            break ;
        }
        out.append(configFile, pos, hash - pos);

        size_t endline = configFile.find('\n', hash);

        if (endline == std::string::npos)
            break ;
        pos = endline;
    }
    configFile.swap(out);
}

static bool hasControlChar(const std::string &s)
{
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\t' || s[i] == '\n' || s[i] == '\r')
            continue ;
        if (s[i] >= 0 && s[i] < 32)
            return (true);
    }
    return (false);
}

size_t findBlockEnd(const std::string &text, size_t open)
{
    size_t depth = 0;

    if (open >= text.size() || text[open] != '{')
        throw std::runtime_error("config: findBlockEnd called off a '{'");
    for (size_t i = open; i < text.size(); ++i)
    {
        if (text[i] == '{')
            depth++;
        else if (text[i] == '}')
        {
            depth--;
            if (depth == 0)
                return (i);
        }
    }
    throw std::runtime_error("config: unclosed '{'");
}

std::vector<std::string> splitServerBlocks(const std::string &text)
{
    const char                  *ws = " \t\n\r\v\f";
    std::vector<std::string>    blocks;
    size_t                      pos = 0;

    while (true)
    {
        size_t open = text.find('{', pos);
        if (open == std::string::npos)
        {
            if (text.find_first_not_of(ws, pos) != std::string::npos)
                throw std::runtime_error("config: stray text outside a server block");
            break ;
        }
        std::string head = text.substr(pos, open - pos);
        size_t first = head.find_first_not_of(ws);
        size_t last = head.find_last_not_of(ws);
        if (first != std::string::npos)
            head = head.substr(first, last - first + 1);
        else
            head.clear();
        if (head != "server")
            throw std::runtime_error("config: expected 'server' before '{', found "
                + quoteValue(head));
        size_t end = findBlockEnd(text, open);
        blocks.push_back(text.substr(open + 1, end - open - 1));
        pos = end + 1;
    }
    if (blocks.empty())
        throw std::runtime_error("config: no server block found");
    return (blocks);
}

static void getStartAndEnd(const std::string &serverBody, size_t &hs, size_t &he,
        size_t headStart, size_t open)
{
    hs = serverBody.find_first_not_of(WHITESPACE, headStart);
    he = serverBody.find_last_not_of(WHITESPACE, open - 1);
    if (hs == std::string::npos || he == std::string::npos || hs > he)
        throw std::runtime_error("config: '{' with no block name");
}

static void checkWord(const std::string &serverBody, size_t &wordEnd,
        size_t hs, size_t he)
{
    wordEnd = serverBody.find_first_of(WHITESPACE, hs);
    if (wordEnd == std::string::npos || wordEnd > he)
        throw std::runtime_error("config: 'location' block with no path");
    std::string word = serverBody.substr(hs, wordEnd - hs);
    if (word != "location")
throw std::runtime_error("config: unknown block " + quoteValue(word)
            + " inside server, expected 'location'");
}

static std::string getPath(const std::string &serverBody, size_t wordEnd, size_t he)
{
    size_t      ps = serverBody.find_first_not_of(WHITESPACE, wordEnd);
    std::string path = serverBody.substr(ps, he - ps + 1);

    if (hasControlChar(path))
        throw std::runtime_error("config: location path has a control character: "
            + quoteValue(path));
    if (path.find_first_of(WHITESPACE) != std::string::npos)
        throw std::runtime_error("config: location path must be one token, got "
            + quoteValue(path));
    return (path);
}

std::vector<LocationBlock> extractLocations(std::string &serverBody)
{
    std::vector<LocationBlock>  locations;
    LocationBlock   location;
    size_t          open;
    size_t          hs;
    size_t          he;
    size_t          wordEnd;

    while ((open = serverBody.find('{')) != std::string::npos)
    {
        size_t  end = findBlockEnd(serverBody, open);
        size_t  headStart = serverBody.find_last_of(';', open);

        if (headStart == std::string::npos)
            headStart = 0;
        else
            headStart = headStart + 1;
        if (open == 0)
            throw std::runtime_error("config: '{' with no block name");
        getStartAndEnd(serverBody, hs, he, headStart, open);
        checkWord(serverBody, wordEnd, hs, he);

        location.path_ = getPath(serverBody, wordEnd, he);
        location.body_ = serverBody.substr(open + 1, end - open - 1);
        locations.push_back(location);
        serverBody.erase(headStart, end - headStart + 1);
    }
    return (locations);
}

std::vector<Directive> splitDirectives(const std::string &body)
{
    std::vector<Directive>  directives;
    size_t                  pos = 0;

    while ((pos = body.find_first_not_of(WHITESPACE, pos)) != std::string::npos)
    {
        size_t semi = body.find(';', pos);
        if (semi == std::string::npos)
            throw std::runtime_error("config: missing ';' after '"
                + body.substr(pos) + "'");

        size_t nameEnd = body.find_first_of(WHITESPACE, pos);
        if (nameEnd == std::string::npos || nameEnd > semi)
            nameEnd = semi;
        std::string name = body.substr(pos, nameEnd - pos);
        if (name.empty())
            throw std::runtime_error("config: stray ';' with no directive");

        Directive   directive;
        std::string args;
        size_t      as = body.find_first_not_of(WHITESPACE, nameEnd);

        if (as < semi)
        {
            size_t ae = body.find_last_not_of(WHITESPACE, semi - 1);
            args = body.substr(as, ae - as + 1);
        }
        if (hasControlChar(name) || hasControlChar(args))
            throw std::runtime_error("config: directive has a control character: "
                + quoteValue(name + " " + args));
        directive.name_ = name;
        directive.args_ = args;
        directives.push_back(directive);
        pos = semi + 1;
    }
    return (directives);
}
