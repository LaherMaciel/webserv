import sys

sys.stdout.write("Content-Type: text/plain\r\n\r\n")
sys.stdout.write("x" * (2 * 1024 * 1024))
