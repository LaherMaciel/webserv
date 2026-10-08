#!/bin/sh

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
cd "$SCRIPT_DIR/.." || exit 1

HOST="127.0.0.1"
PORT="8080"
BASE_URL="http://${HOST}:${PORT}"
UPLOAD_DIR="www/upload"
if [ "$#" -ne 1 ]; then
    printf 'Usage: %s </upload/filename | filename>\n' "$0" >&2
    exit 1
fi
REQUEST_PATH=$1
case "$REQUEST_PATH" in
    /*) ;;
    *) REQUEST_PATH="/upload/${REQUEST_PATH}" ;;
esac
RELATIVE_PATH=${REQUEST_PATH#/upload/}
if [ "$RELATIVE_PATH" = "$REQUEST_PATH" ] || [ -z "$RELATIVE_PATH" ]; then
    printf 'Usage: %s </upload/filename | filename>\n' "$0" >&2
    exit 1
fi
case "$RELATIVE_PATH" in
    */*)
        printf 'DELETE path must contain exactly one filename: %s\n' "$REQUEST_PATH" >&2
        exit 1
        ;;
esac
FILE_NAME=$RELATIVE_PATH
case "$FILE_NAME" in
    "."|".."|*[!A-Za-z0-9._-]*)
        printf 'Unsafe test filename: %s\n' "$FILE_NAME" >&2
        exit 1
        ;;
esac
TARGET_FILE="${UPLOAD_DIR}/${FILE_NAME}"
RESPONSE_BODY="/tmp/webserv-delete-response-$$"

GREEN="$(printf '\033[0;32m')"
RED="$(printf '\033[0;31m')"
RESET="$(printf '\033[0m')"
FAILURES=0

cleanup()
{
    rm -f "$RESPONSE_BODY"
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

delete_request()
{
    curl -sS --connect-timeout 2 --max-time 5 \
        -o "$RESPONSE_BODY" -w '%{http_code}' \
        -X DELETE "${BASE_URL}$1"
}

trap cleanup EXIT HUP INT TERM

if ! command -v curl >/dev/null 2>&1; then
    printf 'curl is required\n' >&2
    exit 1
fi

if [ ! -d "$UPLOAD_DIR" ]; then
    printf 'Upload directory does not exist: %s\n' "$UPLOAD_DIR" >&2
    exit 1
fi

if [ ! -f "$TARGET_FILE" ]; then
    printf 'File to delete does not exist: %s\n' "$TARGET_FILE" >&2
    exit 1
fi

printf 'DELETE %s\n\n' "$REQUEST_PATH"
if HTTP_CODE=$(delete_request "$REQUEST_PATH"); then
    if [ "$HTTP_CODE" = "200" ]; then
        pass "existing file returns 200 OK"
    else
        fail "expected status 200, received ${HTTP_CODE}"
    fi
else
    fail "server accepted the DELETE request"
fi

if [ ! -e "$TARGET_FILE" ]; then
    pass "target file was removed"
else
    fail "target file still exists"
fi

printf '\nDELETE %s again\n\n' "$REQUEST_PATH"
if HTTP_CODE=$(delete_request "$REQUEST_PATH"); then
    if [ "$HTTP_CODE" = "404" ]; then
        pass "missing file returns 404 Not Found"
    else
        fail "expected status 404, received ${HTTP_CODE}"
    fi
else
    fail "server accepted the repeated DELETE request"
fi

printf '\nDELETE /upload\n\n'
if HTTP_CODE=$(delete_request "/upload"); then
    if [ "$HTTP_CODE" = "400" ]; then
        pass "missing filename returns 400 Bad Request"
    else
        fail "expected status 400, received ${HTTP_CODE}"
    fi
else
    fail "server accepted the invalid DELETE request"
fi

printf '\n'
if [ "$FAILURES" -eq 0 ]; then
    printf '%sDELETE tests passed%s\n' "$GREEN" "$RESET"
    exit 0
fi

printf '%sDELETE tests failed: %s check(s)%s\n' "$RED" "$FAILURES" "$RESET"
exit 1
