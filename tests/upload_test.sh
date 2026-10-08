#!/bin/sh

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
cd "$SCRIPT_DIR/.." || exit 1

HOST="127.0.0.1"
PORT="8080"
BASE_URL="http://${HOST}:${PORT}"
UPLOAD_DIR="www/upload"
if [ "$#" -gt 1 ]; then
    printf 'Usage: %s [/upload/filename | filename]\n' "$0" >&2
    exit 1
fi
REQUEST_PATH=${1:-"/upload/webserv-upload-test-$$.bin"}
case "$REQUEST_PATH" in
    /*) ;;
    *) REQUEST_PATH="/upload/${REQUEST_PATH}" ;;
esac
RELATIVE_PATH=${REQUEST_PATH#/upload/}
if [ "$RELATIVE_PATH" = "$REQUEST_PATH" ] || [ -z "$RELATIVE_PATH" ]; then
    printf 'Usage: %s [/upload/filename | filename]\n' "$0" >&2
    exit 1
fi
case "$RELATIVE_PATH" in
    */*)
        printf 'Upload path must contain exactly one filename: %s\n' "$REQUEST_PATH" >&2
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
INPUT_FILE="/tmp/webserv-upload-input-$$"
RESPONSE_BODY="/tmp/webserv-upload-response-$$"
RESPONSE_HEADERS="/tmp/webserv-upload-headers-$$"
UPLOADED_FILE="${UPLOAD_DIR}/${FILE_NAME}"

GREEN="$(printf '\033[0;32m')"
RED="$(printf '\033[0;31m')"
RESET="$(printf '\033[0m')"
FAILURES=0

cleanup()
{
    rm -f "$INPUT_FILE" "$RESPONSE_BODY" "$RESPONSE_HEADERS"
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

trap cleanup EXIT HUP INT TERM

if ! command -v curl >/dev/null 2>&1; then
    printf 'curl is required\n' >&2
    exit 1
fi

if [ ! -d "$UPLOAD_DIR" ]; then
    printf 'Upload directory does not exist: %s\n' "$UPLOAD_DIR" >&2
    exit 1
fi

if [ -e "$UPLOADED_FILE" ] || [ -L "$UPLOADED_FILE" ]; then
    printf 'Refusing to overwrite existing upload: %s\n' "$UPLOADED_FILE" >&2
    exit 1
fi

printf 'webserv upload test\nbytes: \001\002\003\n' > "$INPUT_FILE"

printf 'POST %s\n\n' "$REQUEST_PATH"
if ! HTTP_CODE=$(curl -sS -D "$RESPONSE_HEADERS" -o "$RESPONSE_BODY" \
    --connect-timeout 2 --max-time 5 -w '%{http_code}' \
    --data-binary "@${INPUT_FILE}" \
    "${BASE_URL}${REQUEST_PATH}"); then
    fail "server accepted the connection"
    exit 1
fi

if [ "$HTTP_CODE" = "201" ]; then
    pass "response status is 201 Created"
else
    fail "expected status 201, received ${HTTP_CODE}"
fi

if tr -d '\r' < "$RESPONSE_HEADERS" | grep -Fqx "Location: ${REQUEST_PATH}"; then
    pass "Location header identifies the uploaded resource"
else
    fail "Location header is missing or incorrect"
fi

if [ -f "$UPLOADED_FILE" ]; then
    pass "uploaded file was created"
else
    fail "uploaded file was not created at ${UPLOADED_FILE}"
fi

if [ -f "$UPLOADED_FILE" ] && cmp -s "$INPUT_FILE" "$UPLOADED_FILE"; then
    pass "uploaded bytes match the request body"
else
    fail "uploaded bytes differ from the request body"
fi

printf '\n'
if [ "$FAILURES" -eq 0 ]; then
    printf '%sUpload test passed%s\n' "$GREEN" "$RESET"
    printf 'Uploaded file remains at: %s\n' "$UPLOADED_FILE"
    printf 'Inspect it with:          xxd %s\n' "$UPLOADED_FILE"
    printf 'Delete it through HTTP:   ./tests/delete_test.sh %s\n' "$FILE_NAME"
    exit 0
fi

printf '%sUpload test failed: %s check(s)%s\n' "$RED" "$FAILURES" "$RESET"
printf 'Any file created by the server remains at: %s\n' "$UPLOADED_FILE"
exit 1
