#!/bin/sh

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
cd "$SCRIPT_DIR/.." || exit 1

HOST=${HOST:-127.0.0.1}
PORT=${PORT:-8080}
CGI_PATH=${1:-/cgi-bin/env_dump.py}
URL="http://${HOST}:${PORT}${CGI_PATH}?source=env-test"
RESPONSE_FILE="/tmp/webserv-cgi-env-response-$$"

GREEN="$(printf '\033[0;32m')"
RED="$(printf '\033[0;31m')"
RESET="$(printf '\033[0m')"
FAILURES=0

cleanup()
{
    rm -f "$RESPONSE_FILE"
}

pass()
{
    printf '%sPASS%s  %s\n' "$GREEN" "$RESET" "$1"
}

fail()
{
    printf '%sFAIL%s  %s\n' "$RED" "$RESET" "$1"
    FAILURES=$((FAILURES + 1))
}

expect_line()
{
    if grep -Fqx "$1" "$RESPONSE_FILE"; then
        pass "$2"
    else
        fail "$2 (expected '$1')"
    fi
}

trap cleanup EXIT HUP INT TERM

if ! command -v curl >/dev/null 2>&1; then
    printf 'curl is required\n' >&2
    exit 1
fi

printf 'CGI environment test: %s\n\n' "$URL"

HTTP_CODE=$(curl -sS --connect-timeout 2 --max-time 5 \
    -o "$RESPONSE_FILE" -w '%{http_code}' "$URL") || {
    fail "server accepted the CGI request"
    exit 1
}

if [ "$HTTP_CODE" = "200" ]; then
    pass "CGI returned 200 OK"
else
    fail "expected status 200, received ${HTTP_CODE}"
fi

expect_line "GATEWAY_INTERFACE=CGI/1.1" "CGI interface is set"
expect_line "REQUEST_METHOD=GET" "request method is forwarded"
expect_line "QUERY_STRING=source=env-test" "query string is forwarded"
expect_line "SERVER_PROTOCOL=HTTP/1.1" "request protocol is forwarded"
expect_line "SERVER_SOFTWARE=webserv/1.0" "server software is set"
expect_line "SERVER_PORT=${PORT}" "server port is set"
expect_line "REMOTE_ADDR=${HOST}" "client address is captured"
expect_line "SCRIPT_NAME=${CGI_PATH}" "script URL path is set"
expect_line "HTTP_HOST=${HOST}:${PORT}" "Host header is exported"

SCRIPT_FILENAME=$(sed -n 's/^SCRIPT_FILENAME=//p' "$RESPONSE_FILE")
case "$SCRIPT_FILENAME" in
    */env_dump.py|env_dump.py)
        pass "script filename identifies env_dump.py"
        ;;
    *)
        fail "SCRIPT_FILENAME does not identify env_dump.py (got '${SCRIPT_FILENAME}')"
        ;;
esac

expect_line "SCRIPT_FILENAME_EXISTS=yes" \
    "script filename is usable from the CGI working directory"

SERVER_NAME=$(sed -n 's/^SERVER_NAME=//p' "$RESPONSE_FILE")
case "$SERVER_NAME" in
    ""|"<unset>"|*" "*)
        fail "SERVER_NAME must contain one selected server name (got '${SERVER_NAME}')"
        ;;
    *)
        pass "server name contains one name (${SERVER_NAME})"
        ;;
esac

printf '\nEnvironment returned by CGI:\n'
sed 's/^/  /' "$RESPONSE_FILE"

printf '\n'
if [ "$FAILURES" -eq 0 ]; then
    printf '%sCGI environment test passed%s\n' "$GREEN" "$RESET"
    exit 0
fi

printf '%sCGI environment test failed: %s check(s)%s\n' \
    "$RED" "$FAILURES" "$RESET"
exit 1
