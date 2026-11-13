#!/bin/sh

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
cd "$SCRIPT_DIR/.." || exit 1

HOST=${HOST:-127.0.0.1}
PORT=${PORT:-8080}
RESPONSE_FILE="/tmp/webserv-cgi-half-close-response-$$"

GREEN="$(printf '\033[0;32m')"
RED="$(printf '\033[0;31m')"
YELLOW="$(printf '\033[0;33m')"
RESET="$(printf '\033[0m')"
FAILURES=0

pass()
{
    printf '%sPASS%s  %s\n' "$GREEN" "$RESET" "$1"
}

fail()
{
    printf '%sFAIL%s  %s\n' "$RED" "$RESET" "$1"
    FAILURES=$((FAILURES + 1))
}

cleanup()
{
    rm -f "$RESPONSE_FILE"
}

server_pid()
{
    if ! command -v lsof >/dev/null 2>&1; then
        return
    fi
    lsof -nP -iTCP:"$PORT" -sTCP:LISTEN -t 2>/dev/null | head -n 1
}

fd_count()
{
    lsof -p "$1" 2>/dev/null | awk 'END { print NR }'
}

trap cleanup EXIT HUP INT TERM

if ! command -v curl >/dev/null 2>&1; then
    printf 'curl is required\n' >&2
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
    printf 'python3 is required\n' >&2
    exit 1
fi

printf 'CGI half-close lifecycle test: %s:%s\n\n' "$HOST" "$PORT"

if ! curl -sS --connect-timeout 2 --max-time 2 \
    "http://${HOST}:${PORT}/" >/dev/null; then
    printf '%sFAIL%s  server is not reachable at %s:%s\n' \
        "$RED" "$RESET" "$HOST" "$PORT"
    exit 1
fi

SERVER_PID=$(server_pid)
BASELINE_FDS=""
if [ -n "$SERVER_PID" ]; then
    BASELINE_FDS=$(fd_count "$SERVER_PID")
fi

if python3 - "$HOST" "$PORT" "$RESPONSE_FILE" <<'PY'
import socket
import sys

host = sys.argv[1]
port = int(sys.argv[2])
response_path = sys.argv[3]
body = b"half-closed CGI body"
request = (
    b"POST /cgi-bin/post_echo.py HTTP/1.1\r\n"
    + ("Host: %s:%d\r\n" % (host, port)).encode("ascii")
    + b"Content-Type: text/plain\r\n"
    + ("Content-Length: %d\r\n" % len(body)).encode("ascii")
    + b"\r\n"
    + body
)

client = socket.create_connection((host, port), timeout=2)
try:
    client.sendall(request)
    client.shutdown(socket.SHUT_WR)
    client.settimeout(5)
    chunks = []
    while True:
        chunk = client.recv(4096)
        if not chunk:
            break
        chunks.append(chunk)
finally:
    client.close()

with open(response_path, "wb") as response_file:
    response_file.write(b"".join(chunks))
PY
then
    pass "client half-closed its request side and received a response"
else
    fail "half-closed CGI request completed"
fi

if grep -q '^HTTP/1.1 200' "$RESPONSE_FILE" && \
    grep -q 'CGI POST received' "$RESPONSE_FILE" && \
    grep -q 'Body: half-closed CGI body' "$RESPONSE_FILE"; then
    pass "CGI received the complete body and returned 200 OK"
else
    fail "CGI response was missing, incomplete, or unsuccessful"
fi

STATUS=$(curl -sS --connect-timeout 2 --max-time 2 -o /dev/null \
    -w '%{http_code}' "http://${HOST}:${PORT}/")
if [ "$STATUS" = "200" ]; then
    pass "server still responds after the half-closed CGI request"
else
    fail "server health check returned HTTP ${STATUS}"
fi

if [ -n "$SERVER_PID" ] && [ -n "$BASELINE_FDS" ]; then
    i=0
    FINAL_FDS=$(fd_count "$SERVER_PID")
    while [ "$FINAL_FDS" != "$BASELINE_FDS" ] && [ "$i" -lt 20 ]; do
        sleep 0.05
        FINAL_FDS=$(fd_count "$SERVER_PID")
        i=$((i + 1))
    done
    if [ "$FINAL_FDS" = "$BASELINE_FDS" ]; then
        pass "server fd count returned to baseline (${FINAL_FDS})"
    else
        fail "server fd count changed from ${BASELINE_FDS} to ${FINAL_FDS}"
    fi
else
    printf '%sSKIP%s  fd-count check requires lsof\n' "$YELLOW" "$RESET"
fi

printf '\n'
if [ "$FAILURES" -eq 0 ]; then
    printf '%sCGI half-close lifecycle test passed%s\n' "$GREEN" "$RESET"
    exit 0
fi

printf '%sCGI half-close lifecycle test failed: %s check(s)%s\n' \
    "$RED" "$FAILURES" "$RESET"
exit 1
