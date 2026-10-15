import os
import sys


try:
    content_length = int(os.environ.get("CONTENT_LENGTH", "0"))
except ValueError:
    content_length = 0

body = sys.stdin.buffer.read(content_length)
content_type = os.environ.get("CONTENT_TYPE", "not provided")

response_body = (
    "CGI POST received\n"
    "Content-Length: {0}\n"
    "Bytes read: {1}\n"
    "Content-Type: {2}\n"
    "Body: {3}\n"
).format(
    content_length,
    len(body),
    content_type,
    body.decode("utf-8", errors="replace"),
)

sys.stdout.write("Content-Type: text/plain\r\n")
sys.stdout.write("\r\n")
sys.stdout.write(response_body)
sys.stdout.flush()
