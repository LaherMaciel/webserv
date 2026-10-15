import os
import sys

query = os.environ.get("QUERY_STRING", "")

sys.stdout.write(
    "Content-Type: text/plain\r\n"
    "\r\n"
    "HELLO FROM CGI\n"
    "Query: " + query + "\n"
)