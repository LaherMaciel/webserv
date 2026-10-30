*This project has been created as part of the 42 curriculum by belinore, lawences.*

# webserv

## Description

`webserv` is a non-blocking HTTP server written from scratch in C++98. The goal
of the project is to understand the systems programming behind a web server:
TCP sockets, readiness-based I/O, HTTP message framing, routing, configuration,
filesystem operations, CGI process management, and resource ownership.

The server uses one `poll()`-driven event loop for listening sockets, connected
clients, and CGI pipes. It can serve static websites, accept uploads, delete
resources, generate directory listings, redirect requests, execute CGI scripts,
and host multiple configurations on different addresses or ports.

This is an educational server and is not intended for production deployment.

## Features

- HTTP/1.0 and HTTP/1.1 request handling.
- Persistent HTTP/1.1 connections and buffered pipelined requests.
- `GET`, `POST`, and `DELETE` methods.
- `Content-Length` and chunked request-body framing.
- Partial, non-blocking socket reads and writes.
- Multiple listening sockets, ports, and name-based virtual hosts.
- NGINX-inspired server and location configuration.
- Static file serving and MIME type detection.
- Configurable index files and autoindex directory listings.
- File uploads and deletion from configured locations.
- Configurable HTTP redirects and custom error pages.
- CGI selected by file extension, with non-blocking input and output pipes.
- CGI environment variables, query strings, path info, and request bodies.
- Connection, header, body, and CGI timeouts.
- Request-body and CGI-output size limits.
- Controlled `SIGINT`/`SIGTERM` shutdown with CGI child cleanup.
- Protection against path traversal and malformed request framing.

## Architecture

- **Server** owns listeners and connections and runs the `poll()` event loop.
- **Connection** owns the state of one client, including input/output buffers,
  the current request parser, and any active CGI process.
- **RequestParser** incrementally parses request lines, headers, fixed-length
  bodies, and chunked bodies across multiple reads.
- **Router** selects the best location and produces a static, upload, delete,
  redirect, CGI, or error result.
- **CgiProcess** manages `fork()`, `execve()`, pipes, environment variables,
  timeouts, termination, and child reaping.
- **Response** stores status, headers, and body and serializes them into HTTP.

Ownership is explicit: the server owns network connections, each connection
owns its request and CGI lifecycle, and CGI children close unrelated inherited
file descriptors before `execve()`.

## Instructions

### Requirements

- A Unix-like operating system.
- `make` and a C++ compiler with C++98 support.
- Python 3 for the included Python CGI examples.
- `curl` and `nc` are useful for the supplied tests.

CGI interpreter paths are machine-specific. Check the `cgi_extension`
directives in the selected configuration and adjust paths such as
`/usr/bin/python3` or `/usr/bin/php-cgi` to match the host system.

### Compilation

```sh
make
```

The project is compiled with:

```text
-Wall -Wextra -Werror -std=c++98
```

Other useful targets are:

```sh
make clean      # remove object files
make fclean     # remove object files and the webserv executable
make re         # perform a clean rebuild
make debug      # rebuild with debug symbols and no optimisation
```

### Execution

Run with the default configuration:

```sh
./webserv
```

Run with an explicit configuration:

```sh
./webserv config/default.conf
```

The equivalent Makefile shortcut is:

```sh
make run
```

With `config/default.conf`, the main site is available at
`http://127.0.0.1:8080/` and a second listener is available on port `8081`.
Press `Ctrl-C` to request a controlled shutdown.

## Configuration

The configuration syntax is inspired by NGINX. Directives end with `;`, and
`server` and `location` blocks use braces.

```nginx
server {
    listen 8080;
    host 127.0.0.1;
    server_name localhost webserv.local;
    client_max_body_size 1000000;
    error_page 404 ./errors/404.html;

    location / {
        root ./www;
        index index.html;
        allowed_methods GET;
        autoindex off;
    }

    location /upload {
        root ./www/upload;
        upload_store ./www/upload;
        allowed_methods GET POST DELETE;
        client_max_body_size 5000000;
    }

    location /cgi-bin {
        root ./www/cgi-bin;
        allowed_methods GET POST;
        cgi_extension .py /usr/bin/python3;
    }
}
```

Supported directives include listening host and port, server names, body-size
limits, custom error pages, location roots, index files, allowed methods,
autoindex, upload directories, redirects, and extension-to-CGI-interpreter
mappings. A body-size value of `0` means unlimited.

## Usage Examples

Static content and autoindex:

```sh
curl -i http://127.0.0.1:8080/
curl -i http://127.0.0.1:8080/static/
```

Select a name-based virtual host:

```sh
curl -i -H 'Host: other.local' http://127.0.0.1:8080/
```

Follow a configured redirect:

```sh
curl -i http://127.0.0.1:8080/old-page
```

Upload and then delete a file:

```sh
printf 'hello from webserv\n' > /tmp/webserv-example.txt
curl -i --data-binary @/tmp/webserv-example.txt \
  http://127.0.0.1:8080/upload/example.txt
curl -i -X DELETE http://127.0.0.1:8080/upload/example.txt
```

Run Python CGI with a query string or POST body:

```sh
curl -i 'http://127.0.0.1:8080/cgi-bin/hello.py?name=42'
curl -i -H 'Content-Type: text/plain' \
  --data-binary 'hello from CGI POST' \
  http://127.0.0.1:8080/cgi-bin/post_echo.py
```

## Testing

The `tests/` directory contains targeted regression scripts for HTTP parsing,
request bodies, uploads, deletion, CGI environment variables, large and
concurrent CGI POST bodies, client disconnects, inherited descriptors, and
controlled shutdown.

Build the server first:

```sh
make
```

With the server running, examples include:

```sh
./tests/basic_http_tests.sh
./tests/cgi_post_test.sh
./tests/cgi_env_test.sh
./tests/upload_test.sh
./tests/delete_test.sh example.txt
```

The following lifecycle suites start and stop their own server, so run them
without another instance bound to the configured ports:

```sh
./tests/body_tests.sh
./tests/cgi_shutdown_test.sh
python3 tests/cgi_fd_inheritance_test.py ./webserv
```

Manual testing with a browser, `curl`, `nc` or `telnet`, comparison with NGINX,
and stress testing are also important because no single client exercises every
HTTP or connection-lifecycle edge case.

## Resources

### References

- [RFC 9110: HTTP Semantics](https://www.rfc-editor.org/rfc/rfc9110)
- [RFC 9112: HTTP/1.1](https://www.rfc-editor.org/rfc/rfc9112)
- [RFC 3875: The Common Gateway Interface](https://www.rfc-editor.org/rfc/rfc3875)
- [Linux `poll(2)` manual](https://man7.org/linux/man-pages/man2/poll.2.html)
- [Linux `socket(7)` manual](https://man7.org/linux/man-pages/man7/socket.7.html)
- [Linux `pipe(7)` manual](https://man7.org/linux/man-pages/man7/pipe.7.html)
- [NGINX request processing](https://nginx.org/en/docs/http/request_processing.html)
- [NGINX beginner's guide](https://nginx.org/en/docs/beginners_guide.html)
- [cppreference C++ language reference](https://en.cppreference.com/w/cpp/language.html)

### Videos

- [Web Server Concepts and Examples - WebConcepts](https://www.youtube.com/watch?v=9J1nJOivdyw)
- [Non-blocking I/O and how Node uses it, in friendly terms - Studying With Alex](https://www.youtube.com/watch?v=wB9tIg209-8)

### Use Of AI

AI assistants, including ChatGPT/Codex and Claude, were used as learning and
review tools during development. They helped explain HTTP and CGI concepts,
compare architectural options, review ownership and event-loop decisions,
suggest debugging strategies, identify edge cases, and draft or refine test
scripts and documentation.

Development was iterative and collaborative. The authors built the initial
versions of many features, then used AI suggestions to review, extend, debug,
and test them. AI-generated changes were examined and adapted rather than
accepted blindly; the authors made the final architectural decisions, resolved
integration issues, corrected implementations, and ensured they understood the
resulting process flow and ownership.
