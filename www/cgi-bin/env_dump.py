import os
import sys


VARIABLES = (
    "GATEWAY_INTERFACE",
    "REQUEST_METHOD",
    "QUERY_STRING",
    "SERVER_PROTOCOL",
    "SERVER_SOFTWARE",
    "SERVER_NAME",
    "SERVER_PORT",
    "REMOTE_ADDR",
    "SCRIPT_NAME",
    "SCRIPT_FILENAME",
    "PATH_INFO",
    "CONTENT_TYPE",
    "CONTENT_LENGTH",
    "HTTP_HOST",
)

body = "".join(
    "{0}={1}\n".format(name, os.environ.get(name, "<unset>"))
    for name in VARIABLES
)
script_filename = os.environ.get("SCRIPT_FILENAME", "")
body += "SCRIPT_FILENAME_EXISTS={0}\n".format(
    "yes" if os.path.isfile(script_filename) else "no"
)

sys.stdout.write("Content-Type: text/plain\r\n")
sys.stdout.write("\r\n")
sys.stdout.write(body)
sys.stdout.flush()
