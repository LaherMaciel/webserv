import os
import sys
import time


token = "default"
for field in os.environ.get("QUERY_STRING", "").split("&"):
    key, separator, value = field.partition("=")
    if separator and key == "token" and value.isdigit():
        token = value
        break

pid_path = "/tmp/webserv-cgi-disconnect-{0}.pid".format(token)
with open(pid_path, "w") as pid_file:
    pid_file.write(str(os.getpid()))

time.sleep(30)

sys.stdout.write("Content-Type: text/plain\r\n\r\n")
sys.stdout.write("CGI completed normally\n")
sys.stdout.flush()
