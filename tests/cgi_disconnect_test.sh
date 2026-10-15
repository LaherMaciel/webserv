#!/bin/sh

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
cd "$SCRIPT_DIR/.." || exit 1

HOST=${HOST:-127.0.0.1}
PORT=${PORT:-8080}
TOKEN=$$
PID_FILE="/tmp/webserv-cgi-disconnect-${TOKEN}.pid"
RESPONSE_FILE="/tmp/webserv-cgi-disconnect-response-${TOKEN}"
CURL_ERROR_FILE="/tmp/webserv-cgi-disconnect-curl-${TOKEN}"
URL="http://${HOST}:${PORT}/cgi-bin/disconnect_test.py?token=${TOKEN}"
CURL_PID=""
CGI_PID=""

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
    if [ -n "$CURL_PID" ] && kill -0 "$CURL_PID" 2>/dev/null; then
        kill "$CURL_PID" 2>/dev/null
        wait "$CURL_PID" 2>/dev/null
    fi
    if [ -n "$CGI_PID" ] && kill -0 "$CGI_PID" 2>/dev/null; then
        kill "$CGI_PID" 2>/dev/null
    fi
    rm -f "$PID_FILE" "$RESPONSE_FILE" "$CURL_ERROR_FILE"
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

printf 'CGI disconnect lifecycle test: %s\n\n' "$URL"

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

rm -f "$PID_FILE"
curl -sS --max-time 35 "$URL" >"$RESPONSE_FILE" 2>"$CURL_ERROR_FILE" &
CURL_PID=$!

i=0
while [ ! -s "$PID_FILE" ] && [ "$i" -lt 40 ]; do
    sleep 0.05
    i=$((i + 1))
done

if [ ! -s "$PID_FILE" ]; then
    fail "CGI did not record its PID"
    exit 1
fi

CGI_PID=$(cat "$PID_FILE")
case "$CGI_PID" in
    ''|*[!0-9]*)
        fail "CGI recorded an invalid PID"
        exit 1
        ;;
esac

if kill -0 "$CGI_PID" 2>/dev/null; then
    pass "slow CGI child started with PID ${CGI_PID}"
else
    fail "CGI child exited before the disconnect"
fi

if kill "$CURL_PID" 2>/dev/null; then
    wait "$CURL_PID" 2>/dev/null
    CURL_PID=""
    pass "client disconnected while CGI was running"
else
    fail "client exited before it could be disconnected"
fi

i=0
while kill -0 "$CGI_PID" 2>/dev/null && [ "$i" -lt 40 ]; do
    sleep 0.05
    i=$((i + 1))
done

if kill -0 "$CGI_PID" 2>/dev/null; then
    STATE=$(ps -o state= -p "$CGI_PID" 2>/dev/null | awk '{ print $1 }')
    fail "CGI child remains after disconnect (state: ${STATE:-unknown})"
else
    pass "CGI child was terminated and reaped before its timeout"
    CGI_PID=""
fi

STATUS=$(curl -sS --connect-timeout 2 --max-time 2 -o /dev/null \
    -w '%{http_code}' "http://${HOST}:${PORT}/")
if [ "$STATUS" = "200" ]; then
    pass "server still responds after CGI cleanup"
else
    fail "server health check returned HTTP ${STATUS}"
fi

if [ -n "$SERVER_PID" ] && [ -n "$BASELINE_FDS" ]; then
    sleep 0.2
    FINAL_FDS=$(fd_count "$SERVER_PID")
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
    printf '%sCGI disconnect lifecycle test passed%s\n' "$GREEN" "$RESET"
    exit 0
fi

printf '%sCGI disconnect lifecycle test failed: %s check(s)%s\n' \
    "$RED" "$FAILURES" "$RESET"
exit 1
