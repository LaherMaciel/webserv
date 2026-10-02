# Webserv Subject Requirements

Working reference derived from `subject.pdf`, version 24.1. The PDF remains the
authoritative source if this summary and the original ever appear to disagree.

## Project Scope

- Write an HTTP server in C++98.
- The executable is named `webserv`.
- Invocation: `./webserv [configuration file]`.
- A configuration file may be supplied as an argument or loaded from a default
  path.
- HTTP/1.0 is suggested as a reference point, but is not mandatory.
- The complete HTTP RFC is not required; only the subject's defined subset is
  required.
- The server must work with a standard web browser.
- Virtual hosting is explicitly out of scope, although it may be implemented.

## Build And General Rules

- Compile with `c++` and `-Wall -Wextra -Werror`.
- The code must comply with C++98 and compile with `-std=c++98`.
- The Makefile must provide at least `NAME`, `all`, `clean`, `fclean`, and `re`.
- The Makefile must not perform unnecessary relinking.
- External libraries, including Boost, are forbidden.
- Libft is not authorized.
- Prefer C++ facilities over their C equivalents where possible.
- The program must not crash or terminate unexpectedly under any circumstances.
- Only files present in the submitted Git repository are evaluated.

Files to submit include:

- `Makefile`
- C++ source and header files (`.cpp`, `.h`, `.hpp`, `.tpp`, `.ipp`)
- Configuration files

## Authorized Functions

The subject authorizes:

```text
execve, pipe, strerror, gai_strerror, errno, dup, dup2, fork, socketpair,
htons, htonl, ntohs, ntohl, select, poll, epoll_create, epoll_ctl,
epoll_wait, kqueue, kevent, socket, accept, listen, send, recv, shutdown,
chdir, bind, connect, getaddrinfo, freeaddrinfo, setsockopt, getsockname,
getprotobyname, fcntl, close, read, write, waitpid, kill, signal, access,
stat, open, opendir, readdir, closedir
```

`poll()` may be replaced by an equivalent mechanism such as `select`, `epoll`,
or `kqueue`.

## Event Loop And Non-Blocking I/O

- The server must remain non-blocking at all times.
- Client disconnections must be handled safely.
- Use one `poll()` call, or one equivalent event mechanism, for all server and
  client I/O, including listening sockets.
- The event mechanism must monitor reading and writing simultaneously.
- Never call `read`, `recv`, `write`, or `send` on a socket, pipe, FIFO, or other
  potentially waiting descriptor unless readiness was reported by the event
  mechanism.
- All potentially waiting descriptors, including CGI pipes, must be
  non-blocking.
- Do not inspect `errno` after an I/O operation in order to adjust server
  behavior.
- Regular disk files are exempt from readiness polling.
- Requests must never hang indefinitely.

Calling blocking descriptor I/O without prior readiness is identified by the
subject as a grade-zero failure.

## Mandatory HTTP Behavior

- Serve a complete static website.
- Implement at least `GET`, `POST`, and `DELETE`.
- Accept file uploads.
- Return accurate HTTP status codes.
- Supply built-in default error pages when custom pages are not configured.
- Listen on multiple ports and serve different content from them.
- Remain available under stress testing.
- Compare behavior and response headers with NGINX when uncertain.

Persistent connections, HTTP/1.1 pipelining, cookies, and sessions are not
listed as mandatory requirements. If persistent HTTP/1.1 is implemented,
request and response framing, per-request state reset, connection policy, and
timeouts still need to be correct.

## Configuration Requirements

The syntax may be inspired by the NGINX `server` section. It does not need to
reproduce all of NGINX.

The configuration must support:

- All interface and port pairs on which the program listens.
- Multiple websites/content sets through multiple listening endpoints.
- Default error pages.
- A maximum client request-body size.
- URL or route rules without requiring regular expressions.

Per-route configuration must support:

- Accepted HTTP methods.
- HTTP redirection.
- A filesystem root for requested resources.
- Enabling or disabling directory listing.
- A default file for directory requests.
- Whether uploads are authorized and where uploaded files are stored.
- CGI execution selected by file extension.

The subject's root example uses root-style mapping: if `/kapouet` uses root
`/tmp/www`, then `/kapouet/pouic/toto/pouet` resolves beneath
`/tmp/www/pouic/toto/pouet`.

Additional configuration, such as server names for optional virtual hosting,
is allowed.

## CGI Requirements

- CGI execution must be selected according to file extension.
- Support at least one CGI implementation, such as Python CGI or PHP-CGI.
- Supporting multiple CGI types is bonus functionality.
- `fork()` may only be used for CGI.
- `execve()` must not be used to launch another web server.
- Supply the environment variables required for web-server-to-CGI
  communication.
- The complete request and client-provided arguments must be available to CGI.
- Decode chunked request bodies before passing them to CGI.
- CGI receives EOF as the end of its request body, so the parent must close the
  CGI input pipe after writing the complete decoded body.
- If CGI output has no `Content-Length`, EOF marks the end of its output.
- Run CGI in the correct working directory so relative file access works.
- CGI pipe reads and writes are subject to the same non-blocking, single-event-
  loop requirements as socket I/O.
- A stalled CGI must not make a request hang forever.

Typical per-request CGI values are derived from the request and selected route,
not stored globally in configuration. These include the request method, query
string, script name, script filename, path info, content type, content length,
protocol, request headers, and request body.

## macOS-Specific Rule

On macOS, descriptors must be put into non-blocking mode. The subject restricts
`fcntl()` usage to the listed values:

```text
F_SETFL, O_NONBLOCK, FD_CLOEXEC
```

Any other `fcntl()` flag is forbidden by the subject.

## Required Testing And Demonstration

- Read the relevant HTTP RFC material.
- Test behavior with telnet and NGINX.
- Test with an actual browser.
- Stress test the server and verify that it remains available.
- Do not rely on only one testing program.
- Automated tests may be written in Python, Go, C, or C++.
- The supplied tester is optional but may help identify defects.
- Include configuration files and default files that demonstrate every required
  feature during evaluation.
- Resilience is a central evaluation concern.

## README Requirements

A root-level English `README.md` is mandatory. It must include:

- An italicized first line stating: "This project has been created as part of
  the 42 curriculum by <login1>[, <login2>[, <login3>[...]]]."
- A `Description` section explaining the goal and giving an overview.
- An `Instructions` section covering compilation, installation, and execution
  as applicable.
- A `Resources` section listing conventional references and explaining how AI
  was used, including the tasks and project areas involved.
- Any additional project-specific sections requested by the evaluation rules.

## AI And Understanding

- Review, question, and test AI-generated material.
- Use only work that the team understands and can take responsibility for.
- Seek peer review rather than relying only on AI validation.
- Be prepared to explain architectural choices, parser behavior, pipes, and
  other implementation details during evaluation.

## Bonus

Bonus features are evaluated only when the mandatory part is complete and
correct:

- Cookies and session management, with simple examples.
- Multiple CGI types.

## Submission And Defense

- Submit through the project Git repository.
- Verify all required filenames before evaluation.
- Evaluators may request a small, quickly implementable modification during the
  defense.
- The requested modification is intended to verify understanding and may
  involve behavior, a function, a script, or a data structure.

## High-Risk Compliance Checklist

- [ ] C++98 build passes with all required warning flags.
- [ ] No external or Boost libraries are used.
- [ ] One readiness mechanism drives every socket and pipe read/write.
- [ ] No socket or pipe I/O occurs without a matching readiness event.
- [ ] No post-I/O `errno` check changes server behavior.
- [ ] All waiting descriptors are non-blocking.
- [ ] macOS `fcntl()` usage stays within the subject's allowed values.
- [ ] Client disconnects, partial I/O, timeouts, and CGI cleanup are safe.
- [ ] GET, POST, DELETE, static files, uploads, and accurate errors work.
- [ ] Multiple listening ports and all required route directives work.
- [ ] Chunked request bodies are decoded before CGI execution.
- [ ] CGI input is closed to deliver EOF and CGI output is read through EOF.
- [ ] CGI runs in the correct working directory and cannot hang forever.
- [ ] Default error pages exist when no custom page is configured.
- [ ] Browser, NGINX, telnet, automated, and stress tests have been performed.
- [ ] Demonstration configuration and content cover every mandatory feature.
- [ ] README contains every required section and first-line declaration.
