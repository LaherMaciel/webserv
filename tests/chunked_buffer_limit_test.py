#!/usr/bin/env python3

import os
import socket
import sys


HOST = os.environ.get("HOST", "127.0.0.1")
MAIN_PORT = int(os.environ.get("MAIN_PORT", "8080"))
SMALL_LIMIT_PORT = int(os.environ.get("SMALL_LIMIT_PORT", "8081"))

GREEN = "\033[0;32m"
RED = "\033[0;31m"
RESET = "\033[0m"


def port_is_in_use(port):
    try:
        connection = socket.create_connection((HOST, port), timeout=0.2)
        connection.close()
        return True
    except OSError:
        return False


def send_raw(port, request):
    response = bytearray()
    with socket.create_connection((HOST, port), timeout=2.0) as connection:
        connection.settimeout(5.0)
        connection.sendall(request)
        while True:
            try:
                data = connection.recv(4096)
            except socket.timeout:
                break
            if not data:
                break
            response.extend(data)
    return bytes(response)


def status_code(response):
    first_line = response.split(b"\r\n", 1)[0]
    parts = first_line.split()
    if len(parts) < 2 or not parts[1].isdigit():
        return None
    return int(parts[1])


def request_headers(method, path, port):
    return (
        "%s %s HTTP/1.1\r\n"
        "Host: localhost:%d\r\n"
        "Connection: close\r\n" % (method, path, port)
    ).encode("ascii")


def run_tests():
    failures = 0

    def check(name, expected, request, port):
        nonlocal failures
        try:
            actual = status_code(send_raw(port, request))
        except OSError as error:
            actual = None
            detail = str(error)
        else:
            detail = "HTTP %s" % actual if actual is not None else "no HTTP response"

        if actual == expected:
            print("%sPASS%s  %s" % (GREEN, RESET, name))
        else:
            print("%sFAIL%s  %s" % (RED, RESET, name))
            print("      expected HTTP %d, got %s" % (expected, detail))
            failures += 1

    valid_chunked = (
        request_headers("POST", "/cgi-bin/post_echo.py", MAIN_PORT)
        + b"Content-Type: text/plain\r\n"
        + b"Transfer-Encoding: chunked\r\n\r\n"
        + b"5\r\nhello\r\n0\r\n\r\n"
    )
    check("valid chunked CGI request still succeeds", 200,
          valid_chunked, MAIN_PORT)

    unterminated_size = (
        request_headers("POST", "/", SMALL_LIMIT_PORT)
        + b"Transfer-Encoding: chunked\r\n\r\n"
        + b"a" * 9000
    )
    check("oversized unterminated chunk-size line returns 413", 413,
          unterminated_size, SMALL_LIMIT_PORT)

    unfinished_trailers = (
        request_headers("POST", "/", SMALL_LIMIT_PORT)
        + b"Transfer-Encoding: chunked\r\n\r\n"
        + b"1\r\nx\r\n0\r\nX-Pad: "
        + b"a" * 9000
        + b"\r\n"
    )
    check("oversized unfinished trailer section returns 413", 413,
          unfinished_trailers, SMALL_LIMIT_PORT)

    health_request = request_headers("GET", "/", SMALL_LIMIT_PORT) + b"\r\n"
    check("server remains responsive after malformed requests", 200,
          health_request, SMALL_LIMIT_PORT)

    return failures


def main():
    if not port_is_in_use(MAIN_PORT) or not port_is_in_use(SMALL_LIMIT_PORT):
        print("start webserv with listeners on %s:%d and %s:%d before running this test" %
              (HOST, MAIN_PORT, HOST, SMALL_LIMIT_PORT), file=sys.stderr)
        return 1

    print("Chunked buffer-limit regression tests\n")
    failures = run_tests()
    print()
    if failures == 0:
        print("%sAll chunked buffer-limit tests passed%s" % (GREEN, RESET))
        return 0
    print("%sChunked buffer-limit tests failed: %d%s" %
          (RED, failures, RESET))
    return 1


if __name__ == "__main__":
    sys.exit(main())
