#!/usr/bin/env python3
"""General smoke checks for the default configuration and a running server."""

import concurrent.futures
import http.client
import os
import socket
import sys


HOST = os.environ.get("HOST", "127.0.0.1")
MAIN_PORT = int(os.environ.get("MAIN_PORT", "8080"))
SECOND_PORT = int(os.environ.get("SECOND_PORT", "8081"))
STRESS_REQUESTS = int(os.environ.get("STRESS_REQUESTS", "200"))
STRESS_WORKERS = int(os.environ.get("STRESS_WORKERS", "20"))

GREEN = "\033[0;32m"
RED = "\033[0;31m"
RESET = "\033[0m"


class Results:
    def __init__(self):
        self.passed = 0
        self.failed = 0

    def check(self, name, condition, detail=""):
        if condition:
            self.passed += 1
            print("%sPASS%s  %s" % (GREEN, RESET, name))
            return
        self.failed += 1
        print("%sFAIL%s  %s" % (RED, RESET, name))
        if detail:
            print("      " + detail)

    def run(self, name, test):
        try:
            condition, detail = test()
        except Exception as error:
            self.check(name, False, "%s: %s" % (type(error).__name__, error))
            return
        self.check(name, condition, detail)


def request(port, path="/", host="localhost", method="GET", body=None,
            headers=None):
    client = http.client.HTTPConnection(HOST, port, timeout=5)
    fields = dict(headers or {})
    fields["Host"] = host
    fields["Connection"] = "close"
    try:
        client.request(method, path, body=body, headers=fields)
        response = client.getresponse()
        return response.status, dict((key.lower(), value)
                                     for key, value in response.getheaders()), response.read()
    finally:
        client.close()


class ResponseReader:
    def __init__(self, connection):
        self.connection = connection
        self.buffer = b""

    def _receive_until(self, marker):
        while marker not in self.buffer:
            data = self.connection.recv(4096)
            if not data:
                raise RuntimeError("EOF before complete response headers")
            self.buffer += data

    def read(self, include_body=True):
        self._receive_until(b"\r\n\r\n")
        raw_headers, self.buffer = self.buffer.split(b"\r\n\r\n", 1)
        lines = raw_headers.split(b"\r\n")
        status_fields = lines[0].split()
        if len(status_fields) < 2 or not status_fields[1].isdigit():
            raise RuntimeError("invalid status line: %r" % lines[0])
        headers = {}
        for line in lines[1:]:
            if b":" not in line:
                raise RuntimeError("invalid response header: %r" % line)
            key, value = line.split(b":", 1)
            headers[key.decode("ascii").lower()] = value.strip().decode("latin-1")
        length = int(headers.get("content-length", "0")) if include_body else 0
        while len(self.buffer) < length:
            data = self.connection.recv(4096)
            if not data:
                raise RuntimeError("EOF before complete response body")
            self.buffer += data
        body = self.buffer[:length]
        self.buffer = self.buffer[length:]
        return int(status_fields[1]), headers, body


def raw_request(raw, include_body=True):
    connection = socket.create_connection((HOST, MAIN_PORT), timeout=2)
    connection.settimeout(5)
    try:
        connection.sendall(raw)
        return ResponseReader(connection).read(include_body)
    finally:
        connection.close()


def available(port):
    try:
        connection = socket.create_connection((HOST, port), timeout=.3)
        connection.close()
        return True
    except OSError:
        return False


def has_status(raw, expected, include_body=True):
    status, _, _ = raw_request(raw, include_body)
    return status == expected, "expected HTTP %d, got HTTP %d" % (expected, status)


def routing_tests(results):
    print("\nRouting and configuration")

    def main_site():
        status, headers, body = request(MAIN_PORT)
        condition = (status == 200 and b"Index of webserv" in body
                     and "date" in headers and headers.get("content-type") == "text/html")
        return condition, "expected the main HTML site with Date and Content-Type headers"
    results.run("default server serves the main site", main_site)

    def virtual_host():
        status, _, body = request(MAIN_PORT, host="other.local")
        return status == 200 and b"Other virtual host" in body, \
            "expected other.local to select www/other"
    results.run("Host selects a virtual server on the shared listener", virtual_host)

    def second_listener():
        status, _, body = request(SECOND_PORT)
        return status == 200 and b"Canopy Journal" in body, \
            "expected the distinct site configured on port %d" % SECOND_PORT
    results.run("second port serves different content", second_listener)

    def directory_redirect():
        status, headers, _ = request(MAIN_PORT, "/static?view=all")
        return status == 301 and headers.get("location") == "/static/?view=all", \
            "expected 301 Location: /static/?view=all"
    results.run("directory redirect adds a slash and preserves the query", directory_redirect)

    def configured_redirect():
        status, headers, _ = request(MAIN_PORT, "/old-page")
        return status == 301 and headers.get("location") == "/", \
            "expected configured 301 redirect to /"
    results.run("configured redirect returns Location", configured_redirect)

    def autoindex():
        status, _, body = request(MAIN_PORT, "/static/autoindex-test/")
        text = body.decode("utf-8", "replace")
        condition = (status == 200 and "Index of /static/autoindex-test/" in text
                     and 'href="%3Cscript%3E.txt"' in text
                     and "&lt;script&gt;.txt" in text
                     and 'href="a%20%26%20b.txt"' in text
                     and "a &amp; b.txt" in text
                     and 'href="subdir/"' in text)
        return condition, "expected an escaped listing with URL-encoded links"
    results.run("autoindex escapes labels and encodes links", autoindex)

    def custom_404():
        status, _, body = request(MAIN_PORT, "/missing-evaluation-resource")
        return status == 404 and b"served from errors/404.html" in body, \
            "expected the configured 404 page"
    results.run("main server uses its custom error page", custom_404)

    def second_404():
        status, _, body = request(SECOND_PORT, "/missing-evaluation-resource")
        return status == 404 and b"This path fades out here" in body, \
            "expected the second server's custom 404 page"
    results.run("second listener uses a different custom error page", second_404)

    def mime_type():
        status, headers, _ = request(SECOND_PORT, "/static/site.css")
        return status == 200 and headers.get("content-type") == "text/css", \
            "expected text/css, got %r" % headers.get("content-type")
    results.run("static files receive an appropriate MIME type", mime_type)


def protocol_tests(results):
    print("\nRequest validation and status codes")
    cases = [
        ("HTTP/1.1 requires Host", 400,
         b"GET / HTTP/1.1\r\nConnection: close\r\n\r\n", True),
        ("unknown token method returns 501", 501,
         b"BREW / HTTP/1.1\r\nHost: localhost\r\n\r\n", True),
        ("malformed method returns 400", 400,
         b"G@T / HTTP/1.1\r\nHost: localhost\r\n\r\n", True),
        ("known but disallowed method returns 405", 405,
         b"POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n", True),
        ("HEAD is parsed but rejected by location policy", 405,
         b"HEAD / HTTP/1.1\r\nHost: localhost\r\n\r\n", False),
        ("unsupported HTTP version returns 505", 505,
         b"GET / HTTP/2.0\r\nHost: localhost\r\n\r\n", True),
        ("malformed header returns 400", 400,
         b"GET / HTTP/1.1\r\nHost: localhost\r\nBroken\r\n\r\n", True),
        ("decoded traversal is rejected", 400,
         b"GET /static/%2e%2e/index.html HTTP/1.1\r\nHost: localhost\r\n\r\n", True),
    ]
    for name, expected, raw, include_body in cases:
        results.run(name, lambda raw=raw, expected=expected, include_body=include_body:
                    has_status(raw, expected, include_body))

    oversized = (b"GET / HTTP/1.1\r\nHost: localhost\r\nX-Pad: "
                 + b"x" * 8200 + b"\r\n\r\n")
    results.run("oversized complete headers return 431",
                lambda: has_status(oversized, 431))

    def listener_body_limit():
        status, _, _ = request(SECOND_PORT, method="POST", body=b"x" * 101,
                               headers={"Content-Type": "application/octet-stream"})
        return status == 413, "expected HTTP 413, got HTTP %d" % status
    results.run("selected listener enforces its body-size limit", listener_body_limit)


def connection_tests(results):
    print("\nConnection policy")

    def persistent():
        connection = socket.create_connection((HOST, MAIN_PORT), timeout=2)
        connection.settimeout(5)
        reader = ResponseReader(connection)
        try:
            connection.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")
            first = reader.read()
            connection.sendall(b"GET /about.html HTTP/1.1\r\nHost: localhost\r\n\r\n")
            second = reader.read()
        finally:
            connection.close()
        condition = (first[0] == 200 and b"Index of webserv" in first[2]
                     and second[0] == 200 and b"about" in second[2].lower())
        return condition, "expected two complete responses on one socket"
    results.run("HTTP/1.1 handles sequential persistent requests", persistent)

    def closes(raw):
        connection = socket.create_connection((HOST, MAIN_PORT), timeout=2)
        connection.settimeout(3)
        try:
            connection.sendall(raw)
            status, headers, _ = ResponseReader(connection).read()
            eof = connection.recv(1) == b""
            condition = status == 200 and headers.get("connection") == "close" and eof
            return condition, "expected HTTP 200, Connection: close, then EOF"
        finally:
            connection.close()

    results.run("Connection: close closes HTTP/1.1 after its response",
                lambda: closes(b"GET / HTTP/1.1\r\nHost: localhost\r\n"
                               b"Connection: close\r\n\r\n"))
    results.run("HTTP/1.0 closes by default",
                lambda: closes(b"GET / HTTP/1.0\r\n\r\n"))


def stress_tests(results):
    print("\nBounded concurrency")

    def one_request(_):
        status, _, body = request(MAIN_PORT)
        return status == 200 and b"Index of webserv" in body

    def concurrent_requests():
        with concurrent.futures.ThreadPoolExecutor(max_workers=STRESS_WORKERS) as executor:
            outcomes = list(executor.map(one_request, range(STRESS_REQUESTS)))
        completed = sum(1 for outcome in outcomes if outcome)
        return completed == STRESS_REQUESTS, "%d/%d requests succeeded" % \
            (completed, STRESS_REQUESTS)

    results.run("%d concurrent static requests complete" % STRESS_REQUESTS,
                concurrent_requests)


def main():
    missing = [str(port) for port in (MAIN_PORT, SECOND_PORT) if not available(port)]
    if missing:
        print("Start webserv with config/default.conf before running this test.",
              file=sys.stderr)
        print("Unavailable port(s): " + ", ".join(missing), file=sys.stderr)
        return 1

    print("Webserv smoke tests: %s:%d and %s:%d" %
          (HOST, MAIN_PORT, HOST, SECOND_PORT))
    results = Results()
    routing_tests(results)
    protocol_tests(results)
    connection_tests(results)
    stress_tests(results)

    print("\n%d passed, %d failed" % (results.passed, results.failed))
    return 1 if results.failed else 0


if __name__ == "__main__":
    sys.exit(main())
