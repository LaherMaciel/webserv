#ifndef RESPONSE_HPP
# define RESPONSE_HPP


class Response
{
    public:
        std::map<std::string, std::string> header;
        std::string version;
        int         code;
        std::string body;

        Response();
        Response(std::string version, int code);
        ~Response();
        Response(const Response& other);
        Response& operator=(const Response& other);

        void    buildBody(Request request);
        void	buildHeader();
        void    buildResponse(Request request);
        std::string getResponse();
};

#endif