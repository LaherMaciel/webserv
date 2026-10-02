/**
 * A config file holds ONE OR MORE server blocks, so the parser has to return
 * a std::vector<ServerConfig>, not a single one.
 *
 * @param listen @example 4242
 * @param host @example 127.0.0.1
 * @param server_name @example webserv
 * @param error_page @example 404 /errors/404.html
 *      several codes may share one page: "error_page 500 502 503 /errors/50x.html;"
 *      so read tokens until the ';' and treat the last one as the path
 * @param client_max_body_size @example 1000000
 * @param location @param path {
 *      @param root @example ./www
 *      @param index @example index.html
 *      @param allowed_methods @example GET POST DELETE
 *      @param autoindex @example off
 *          directory listing. When the path is a directory and it holds no
 *          'index' file: on -> build an HTML page listing the directory
 *          contents, off -> 403 Forbidden.
 *      @param upload_store @example ./www/uploads
 *          where POSTed files are written. No value -> uploads not authorized.
 *      @param return @example 301 /new-place
 *          HTTP redirection: a status code AND a target. Code 0 = no redirect.
 *      @param cgi_extension @example .py /usr/bin/python3
 *          picks the interpreter by file extension. May appear more than once
 *          in the same block, so it is a list (one entry per extension).
 * }
 *
 * what is not found here, we can make to use a default:
 *      autoindex               off
 *      index                   index.html
 *      allowed_methods         GET
 *      client_max_body_size    1000000
 *      upload_store            (empty - uploads refused)
 *      return                  0 (no redirect)
 * make so we can have more then one path
 */
