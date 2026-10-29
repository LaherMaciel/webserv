#!/bin/sh

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
cd "$SCRIPT_DIR/.." || exit 1

HOST=${HOST:-127.0.0.1}
PORT=${PORT:-8080}
CGI_PATH=${1:-"/cgi-bin/post_echo.py"}
URL="http://${HOST}:${PORT}${CGI_PATH}"
LONG_SIZE=524288

SHORT_INPUT="/tmp/webserv-cgi-short-input-$$"
LONG_INPUT="/tmp/webserv-cgi-long-input-$$"
SHORT_RESPONSE="/tmp/webserv-cgi-short-response-$$"
LONG_RESPONSE="/tmp/webserv-cgi-long-response-$$"
CONCURRENT_ONE="/tmp/webserv-cgi-concurrent-one-$$"
CONCURRENT_TWO="/tmp/webserv-cgi-concurrent-two-$$"
CODE_ONE="/tmp/webserv-cgi-code-one-$$"
CODE_TWO="/tmp/webserv-cgi-code-two-$$"

GREEN="$(printf '\033[0;32m')"
RED="$(printf '\033[0;31m')"
RESET="$(printf '\033[0m')"
FAILURES=0

cleanup()
{
    rm -f "$SHORT_INPUT" "$LONG_INPUT" "$SHORT_RESPONSE" "$LONG_RESPONSE" \
        "$CONCURRENT_ONE" "$CONCURRENT_TWO" "$CODE_ONE" "$CODE_TWO"
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

response_has_size()
{
    grep -Fqx "Content-Length: $2" "$1" &&
        grep -Fqx "Bytes read: $2" "$1"
}

send_body()
{
    curl -sS --connect-timeout 2 --max-time 20 \
        -H 'Content-Type: application/octet-stream' \
        --data-binary "@$1" \
        -o "$2" -w '%{http_code}' "$URL"
}

trap cleanup EXIT HUP INT TERM

if ! command -v curl >/dev/null 2>&1; then
    printf 'curl is required\n' >&2
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
    printf 'python3 is required to generate the large test body\n' >&2
    exit 1
fi

printf 'hello from CGI POST' > "$SHORT_INPUT"
python3 -c 'import sys; sys.stdout.buffer.write(b"x" * 524288)' > "$LONG_INPUT"

printf 'CGI POST tests: %s\n\n' "$URL"

printf '1. Short request body\n'
if SHORT_CODE=$(send_body "$SHORT_INPUT" "$SHORT_RESPONSE") && \
    [ "$SHORT_CODE" = "200" ] && response_has_size "$SHORT_RESPONSE" 19; then
    pass "CGI received all 19 bytes"
else
    fail "short CGI POST did not return 200 with all 19 bytes"
fi

printf '\n2. Body larger than the pipe buffer\n'
if LONG_CODE=$(send_body "$LONG_INPUT" "$LONG_RESPONSE") && \
    [ "$LONG_CODE" = "200" ] && response_has_size "$LONG_RESPONSE" "$LONG_SIZE"; then
    pass "CGI received all ${LONG_SIZE} bytes"
else
    fail "large CGI POST did not return 200 with all ${LONG_SIZE} bytes"
fi

printf '\n3. Two concurrent large CGI requests\n'
curl -sS --connect-timeout 2 --max-time 20 \
    -H 'Content-Type: application/octet-stream' \
    --data-binary "@$LONG_INPUT" \
    -o "$CONCURRENT_ONE" -w '%{http_code}' "$URL" > "$CODE_ONE" &
PID_ONE=$!

curl -sS --connect-timeout 2 --max-time 20 \
    -H 'Content-Type: application/octet-stream' \
    --data-binary "@$LONG_INPUT" \
    -o "$CONCURRENT_TWO" -w '%{http_code}' "$URL" > "$CODE_TWO" &
PID_TWO=$!

WAIT_ONE=0
WAIT_TWO=0
wait "$PID_ONE" || WAIT_ONE=$?
wait "$PID_TWO" || WAIT_TWO=$?

if [ "$WAIT_ONE" -eq 0 ] && [ "$WAIT_TWO" -eq 0 ] && \
    [ "$(cat "$CODE_ONE")" = "200" ] && [ "$(cat "$CODE_TWO")" = "200" ] && \
    response_has_size "$CONCURRENT_ONE" "$LONG_SIZE" && \
    response_has_size "$CONCURRENT_TWO" "$LONG_SIZE"; then
    pass "both concurrent CGI requests received all ${LONG_SIZE} bytes"
else
    fail "one or both concurrent CGI requests failed"
fi

printf '\n'
if [ "$FAILURES" -eq 0 ]; then
    printf '%sAll CGI POST tests passed%s\n' "$GREEN" "$RESET"
    exit 0
fi

printf '%sCGI POST tests failed: %s check(s)%s\n' "$RED" "$FAILURES" "$RESET"
exit 1
