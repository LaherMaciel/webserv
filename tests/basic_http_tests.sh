#!/bin/sh

HOST="127.0.0.1"
PORT="8080"
CYAN="$(printf '\033[0;36m')"
RED="$(printf '\033[0;31m')"
GREEN="$(printf '\033[0;32m')"
YELLOW="$(printf '\033[0;33m')"
RESET="$(printf '\033[0m')"

run_test()
{
    number="$1"
    request="$2"

    printf '%s\n' "$YELLOW$number$RESET"
    printf '%b' "$request" | sed -n l
    printf '%s\nResponse:\n%s' "$YELLOW" "$RESET"
    printf '%s' "$CYAN"
    (printf '%b' "$request"; sleep 1) | nc "$HOST" "$PORT" | awk '{ print } /^\r$/ { exit }'
    printf '%s' "$RESET"
    printf '\n'
}

printf "%s===== VALID =====\n%s" "$GREEN" "$RESET"

run_test "Request Val.1:" \
'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n'

run_test "Request Val.2:" \
'GET / HTTP/1.0\r\n\r\n'

printf "%s===== INVALID =====\n%s" "$RED" "$RESET"

run_test "Request Inv.1:" \
'GET /notfound HTTP/1.1\r\nHost: localhost\r\n\r\n'

run_test "Request Inv.2:" \
'POST / HTTP/1.1\r\nHost: localhost\r\n\r\n'
