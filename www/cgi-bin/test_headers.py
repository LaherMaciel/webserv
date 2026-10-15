import os
import sys


def get_mode():
    query = os.environ.get("QUERY_STRING", "")
    for item in query.split("&"):
        key, separator, value = item.partition("=")
        if separator and key == "mode":
            return value
    return "valid"


mode = get_mode()
newline = "\n" if mode == "lf" else "\r\n"

headers = [
    "Status: 201 Created",
    "content-type: text/plain",
    "X-CGI-Test: " + mode,
]

if mode == "duplicate":
    headers.append("Content-Type: text/html")
elif mode == "malformed":
    headers.append("Broken-Header")
else:
    # The server should ignore these CGI framing headers and generate its own.
    headers.append("Content-Length: 9999")
    headers.append("Connection: keep-alive")
    headers.append("Transfer-Encoding: chunked")

body = "CGI header test passed\nMode: " + mode + "\n"
sys.stdout.write(newline.join(headers) + newline + newline + body)
sys.stdout.flush()
