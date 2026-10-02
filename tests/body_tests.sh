#!/bin/bash
# webserv body / framing tests
# usage: ./tests/body_tests.sh        (run from the project root)
#
# These encode the TARGET behaviour, so some will fail until the body parser
# is done. Each test prints what it expects so a failure tells you which rule
# is not implemented yet.
#
# POST_PATH: a route that allows POST. Change it if your config differs.

HOST=127.0.0.1
PORT=8080
BIN=./webserv
POST_PATH=/upload

PASS=0
FAIL=0

# --- helpers ---------------------------------------------------------------

# send() <raw bytes>              send in one write, keep connection 1s
send() {
    (printf "$1"; sleep 1) | nc -w 2 $HOST $PORT 2>/dev/null
}

# send2() <part1> <part2>         two writes 1s apart = two recv() calls
send2() {
    (printf "$1"; sleep 1; printf "$2"; sleep 1) | nc -w 2 $HOST $PORT 2>/dev/null
}

# count status lines in a reply
nresp() { echo "$1" | grep -c '^HTTP/1\.1'; }

pass() { printf "  \033[32mPASS\033[0m  %s\n" "$1"; PASS=$((PASS + 1)); }
fail() {
    printf "  \033[31mFAIL\033[0m  %s\n" "$1"
    printf "        expected: %s\n" "$2"
    printf "        got:      %s\n" "$3"
    FAIL=$((FAIL + 1))
}

# expect_status <name> <regex> <reply>
expect_status() {
    local first; first=$(echo "$3" | grep '^HTTP/1\.1' | head -1 | tr -d '\r')
    if echo "$first" | grep -qE "$2"; then pass "$1"
    else fail "$1" "status matching '$2'" "${first:-<no response>}"; fi
}

# expect_count <name> <n> <reply>
expect_count() {
    local n; n=$(nresp "$3")
    if [ "$n" -eq "$2" ]; then pass "$1"
    else fail "$1" "$2 responses" "$n responses: $(echo "$3" | grep '^HTTP' | tr -d '\r' | tr '\n' '|')"; fi
}

# expect_silence <name> <reply>    no response at all = server is waiting
expect_silence() {
    if [ -z "$(echo "$2" | grep '^HTTP')" ]; then pass "$1"
    else fail "$1" "no response (server should wait)" "$(echo "$2" | head -1 | tr -d '\r')"; fi
}

start_server() {
    pkill -f "$BIN\$" 2>/dev/null; sleep 0.3
    $BIN > /tmp/webserv_body.log 2>&1 &
    sleep 1
    nc -z $HOST $PORT 2>/dev/null || { echo "server not listening on $PORT"; exit 1; }
}
stop_server() { pkill -f "$BIN\$" 2>/dev/null; }

start_server
trap stop_server EXIT

GET_ROOT='GET / HTTP/1.1\r\nHost: x\r\n\r\n'

# ===========================================================================
echo; echo "A. NO BODY"

r=$(send "$GET_ROOT")
expect_status "GET without body -> 200" "200" "$r"

r=$(send "${GET_ROOT}${GET_ROOT}")
expect_count "two bodiless requests in one read -> 2 responses" 2 "$r"

# ===========================================================================
echo; echo "B. CONTENT-LENGTH"

CL5="POST $POST_PATH HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\nhello"

r=$(send "$CL5")
expect_status "Content-Length body -> 2xx" "20[0-9]" "$r"

r=$(send "${CL5}${GET_ROOT}")
expect_count "CL body then GET in one read -> 2 responses" 2 "$r"

r=$(send2 "POST $POST_PATH HTTP/1.1\r\nHost: x\r\nContent-Length: 10\r\n\r\nhello" " world")
expect_count "CL body split mid-body across 2 reads -> 1 response" 1 "$r"

r=$(send2 "POST $POST_PATH HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\n" "hello")
expect_count "headers in read 1, whole body in read 2 -> 1 response" 1 "$r"

r=$(send "POST $POST_PATH HTTP/1.1\r\nHost: x\r\nContent-Length: 10\r\n\r\nhello")
expect_silence "CL 10 but only 5 bytes sent -> server waits, no response" "$r"

r=$(send "POST $POST_PATH HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\nhelloGET / HTTP/1.1\r\nHost: x\r\n\r\n")
expect_count "bytes after N belong to the NEXT request -> 2 responses" 2 "$r"

r=$(send "POST $POST_PATH HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n")
expect_status "Content-Length: 0 -> 2xx, empty body" "20[0-9]" "$r"

r=$(send "POST $POST_PATH HTTP/1.1\r\nHost: x\r\nContent-Length: abc\r\n\r\n")
expect_status "Content-Length: abc -> 400" "400" "$r"

r=$(send "POST $POST_PATH HTTP/1.1\r\nHost: x\r\nContent-Length: -5\r\n\r\n")
expect_status "Content-Length: -5 -> 400" "400" "$r"

r=$(send "POST $POST_PATH HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\nContent-Length: 7\r\n\r\nhello")
expect_status "two different Content-Length headers -> 400" "400" "$r"

r=$(send "POST $POST_PATH HTTP/1.1\r\nHost: x\r\nContent-Length:   5   \r\n\r\nhello")
expect_status "Content-Length with padding spaces -> still 2xx" "20[0-9]" "$r"

big=$(head -c 5000 /dev/zero | tr '\0' 'x')
r=$(send "POST $POST_PATH HTTP/1.1\r\nHost: x\r\nContent-Length: 5000\r\n\r\n$big")
expect_status "5000-byte body (bigger than recv buffer) -> 2xx" "20[0-9]" "$r"

r=$(send "GET / HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\nhello")
expect_status "GET with a Content-Length body is legal -> 200" "200" "$r"

# ===========================================================================
echo; echo "C. TRANSFER-ENCODING: CHUNKED"
echo "   (until chunked is implemented, 501 is the correct stub answer)"

TE_HDR="POST $POST_PATH HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n"

r=$(send "${TE_HDR}5\r\nhello\r\n0\r\n\r\n")
expect_status "one chunk -> 2xx (or 501 stub)" "20[0-9]|501" "$r"

r=$(send "${TE_HDR}5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n")
expect_status "two chunks -> 2xx (or 501 stub)" "20[0-9]|501" "$r"

r=$(send "${TE_HDR}a\r\n0123456789\r\n0\r\n\r\n")
expect_status "hex chunk size (a = 10) -> 2xx (or 501 stub)" "20[0-9]|501" "$r"

r=$(send "${TE_HDR}5\r\nhello\r\n0\r\n\r\n${GET_ROOT}")
expect_count "chunked then GET in one read -> 2 responses" 2 "$r"

r=$(send2 "${TE_HDR}5\r\nhel" "lo\r\n0\r\n\r\n")
expect_count "chunk split mid-data across 2 reads -> 1 response" 1 "$r"

r=$(send2 "${TE_HDR}5\r\nhello\r\n" "0\r\n\r\n")
expect_count "terminator chunk arrives in read 2 -> 1 response" 1 "$r"

r=$(send "${TE_HDR}5\r\nhello\r\n")
expect_silence "no terminating 0 chunk -> server waits, no response" "$r"

r=$(send "${TE_HDR}zz\r\nhello\r\n0\r\n\r\n")
expect_status "non-hex chunk size -> 400" "400" "$r"

r=$(send "${TE_HDR}0\r\n\r\n")
expect_status "empty chunked body (just 0) -> 2xx (or 501 stub)" "20[0-9]|501" "$r"

# ===========================================================================
echo; echo "D. BOTH HEADERS  (Transfer-Encoding must win)"

BOTH="POST $POST_PATH HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n"

r=$(send "${BOTH}0\r\n\r\n")
expect_status "both headers, empty chunked body -> 2xx (or 501), not hung" "20[0-9]|501" "$r"

# If CL were trusted, the server would eat 5 bytes ('0\r\n\r\nG') and never
# see the GET. Two responses proves chunked framing was used.
r=$(send "${BOTH}0\r\n\r\n${GET_ROOT}")
expect_count "both headers then GET -> 2 responses (proves TE was used)" 2 "$r"

# ===========================================================================
echo; echo "E. LIMITS  (needs the config's client_max_body_size)"

r=$(send "POST $POST_PATH HTTP/1.1\r\nHost: x\r\nContent-Length: 99999999\r\n\r\n")
expect_status "Content-Length over client_max_body_size -> 413" "413" "$r"

# ===========================================================================
echo; echo "F. SURVIVAL"

if nc -z $HOST $PORT 2>/dev/null; then pass "server still alive after all of the above"
else fail "server still alive after all of the above" "listening" "dead"; fi

echo
echo "-----------------------------"
printf "  %s passed, %s failed\n" "$PASS" "$FAIL"
echo "-----------------------------"
echo
[ "$FAIL" -eq 0 ]
