#!/usr/bin/env python3
"""Deliberately slow checks for header and CGI execution deadlines."""

import http.client
import os
import socket
import sys
import threading
import time


HOST = os.environ.get("HOST", "127.0.0.1")
PORT = int(os.environ.get("PORT", "8080"))
CGI_PATH = os.environ.get("CGI_PATH", "/cgi-bin/timeout_test.py")

GREEN = "\033[0;32m"
RED = "\033[0;31m"
RESET = "\033[0m"


def report(name, condition, detail=""):
    colour = GREEN if condition else RED
    label = "PASS" if condition else "FAIL"
    print("%s%s%s  %s" % (colour, label, RESET, name))
    if detail and not condition:
        print("      " + detail)
    return 0 if condition else 1


def cgi_request(result):
    client = http.client.HTTPConnection(HOST, PORT, timeout=40)
    try:
        client.request("GET", CGI_PATH, headers={"Host": "localhost",
                                                 "Connection": "close"})
        response = client.getresponse()
        result.append((response.status, response.read()))
    except Exception as error:
        result.append(error)
    finally:
        client.close()


def main():
    failures = 0
    try:
        slow_header = socket.create_connection((HOST, PORT), timeout=2)
    except OSError as error:
        print("Start webserv with config/default.conf before running this test: %s" % error,
              file=sys.stderr)
        return 1

    print("Timeout lifecycle tests (approximately 30 seconds)\n")
    cgi_result = []
    cgi_thread = threading.Thread(target=cgi_request, args=(cgi_result,))
    cgi_thread.start()

    started = time.monotonic()
    slow_header.settimeout(15)
    try:
        slow_header.sendall(b"GET / HTTP/1.1\r\nHost: localhost")
        header_eof = slow_header.recv(1) == b""
    except socket.timeout:
        header_eof = False
    finally:
        slow_header.close()
    header_elapsed = time.monotonic() - started
    failures += report("incomplete headers are closed at the header deadline",
                       header_eof and 8 <= header_elapsed <= 15,
                       "connection did not close near the 10-second deadline")

    cgi_thread.join(40)
    if cgi_thread.is_alive():
        failures += report("stalled CGI is terminated", False,
                           "request was still pending after 40 seconds")
    elif len(cgi_result) != 1 or isinstance(cgi_result[0], Exception):
        failures += report("stalled CGI is terminated", False,
                           "CGI request failed without an HTTP response: %r" % cgi_result)
    else:
        failures += report("stalled CGI returns 504 Gateway Timeout",
                           cgi_result[0][0] == 504,
                           "expected HTTP 504, got HTTP %d" % cgi_result[0][0])

    try:
        health = http.client.HTTPConnection(HOST, PORT, timeout=3)
        health.request("GET", "/", headers={"Host": "localhost",
                                             "Connection": "close"})
        response = health.getresponse()
        healthy = response.status == 200
        response.read()
        health.close()
    except Exception:
        healthy = False
    failures += report("server remains responsive after both timeouts", healthy)

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
