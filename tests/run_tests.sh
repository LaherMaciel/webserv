#!/bin/bash
# webserv test suite
# usage: ./tests/run_tests.sh        (run from the project root)
#
# Each test sends raw bytes with nc and checks what comes back.
# The `sleep 1` after printf keeps the connection open long enough
# for the server to answer before nc closes it.

HOST=127.0.0.1
PORT=8080
BIN=./webserv

PASS=0
FAIL=0

# --- helpers ---------------------------------------------------------------

# send raw bytes, print the reply
send() {
    (printf "$1"; sleep 1) | nc -w 2 $HOST $PORT 2>/dev/null
}

# check() <name> <expected substring> <raw request>
check() {
    local name="$1" expect="$2" req="$3"
    local out
    out=$(send "$req")
    if echo "$out" | grep -q "$expect"; then
        printf "  \033[32mPASS\033[0m  %s\n" "$name"
        PASS=$((PASS + 1))
    else
        printf "  \033[31mFAIL\033[0m  %s\n" "$name"
        printf "        expected: %s\n" "$expect"
        printf "        got:      %s\n" "$(echo "$out" | head -1)"
        FAIL=$((FAIL + 1))
    fi
}

start_server() {
    pkill -f "$BIN" 2>/dev/null
    sleep 0.3
    $BIN > /tmp/webserv_test.log 2>&1 &
    sleep 1
    if ! nc -z $HOST $PORT 2>/dev/null; then
        echo "server failed to start on $PORT - see /tmp/webserv_test.log"
        exit 1
    fi
}

stop_server() {
    pkill -f "$BIN" 2>/dev/null
}

# --- tests -----------------------------------------------------------------

start_server
trap stop_server EXIT

echo
echo "START LINE"
check "valid GET returns 200" \
      "HTTP/1.1 200" \
      'GET / HTTP/1.1\r\nHost: x\r\n\r\n'

check "unknown method returns 501" \
      "501" \
      'BREW / HTTP/1.1\r\nHost: x\r\n\r\n'

check "url without leading slash returns 400" \
      "400" \
      'GET index.html HTTP/1.1\r\nHost: x\r\n\r\n'

check "empty url returns 400" \
      "400" \
      'GET  HTTP/1.1\r\nHost: x\r\n\r\n'

check "over-long url returns 414" \
      "414" \
      "GET /$(printf 'a%.0s' $(seq 1 3000)) HTTP/1.1\r\nHost: x\r\n\r\n"

echo
echo "HEADERS"
check "header with no colon returns 400" \
      "400" \
      'GET / HTTP/1.1\r\nGarbageNoColon\r\n\r\n'

check "multiple headers all parsed" \
      "User-Agent" \
      'GET / HTTP/1.1\r\nHost: x\r\nUser-Agent: test\r\nAccept: */*\r\n\r\n'

echo
echo "PIPELINING  (two requests in one recv)"
# Both replies must come back, in order.
out=$(send 'GET / HTTP/1.1\r\nHost: x\r\n\r\nGET /second HTTP/1.1\r\nHost: x\r\n\r\n')
n=$(echo "$out" | grep -c "^HTTP/1.1")
if [ "$n" -eq 2 ]; then
    printf "  \033[32mPASS\033[0m  two requests get two responses\n"
    PASS=$((PASS + 1))
else
    printf "  \033[31mFAIL\033[0m  two requests get two responses (got %s)\n" "$n"
    FAIL=$((FAIL + 1))
fi

# A failure in the second request must not swallow the first response.
out=$(send 'GET / HTTP/1.1\r\nHost: x\r\n\r\nBREW / HTTP/1.1\r\nHost: x\r\n\r\n')
if echo "$out" | grep -q "200" && echo "$out" | grep -q "501"; then
    printf "  \033[32mPASS\033[0m  good request answered before bad one fails\n"
    PASS=$((PASS + 1))
else
    printf "  \033[31mFAIL\033[0m  good request answered before bad one fails\n"
    printf "        got: %s\n" "$(echo "$out" | grep '^HTTP' | tr '\n' ' ')"
    FAIL=$((FAIL + 1))
fi

echo
echo "PARTIAL DELIVERY  (one request split across two recv calls)"
# The server must wait for \r\n\r\n instead of answering early.
out=$( (printf 'GET / HTTP/1.1\r\nHost: '; sleep 1; printf 'x\r\n\r\n'; sleep 1) | nc -w 2 $HOST $PORT 2>/dev/null )
if echo "$out" | grep -q "HTTP/1.1 200"; then
    printf "  \033[32mPASS\033[0m  request split mid-header still answered\n"
    PASS=$((PASS + 1))
else
    printf "  \033[31mFAIL\033[0m  request split mid-header still answered\n"
    FAIL=$((FAIL + 1))
fi

echo
echo "CONTENT-LENGTH"
# curl enforces the contract: it reads exactly N bytes and errors if
# the body does not match what the header promised.
if curl -s --max-time 3 -o /dev/null http://$HOST:$PORT/ ; then
    printf "  \033[32mPASS\033[0m  curl accepts the response framing\n"
    PASS=$((PASS + 1))
else
    printf "  \033[31mFAIL\033[0m  curl rejects the response framing\n"
    FAIL=$((FAIL + 1))
fi

declared=$(curl -s --max-time 3 -D - -o /dev/null http://$HOST:$PORT/ | grep -i '^Content-Length' | tr -d '\r' | awk '{print $2}')
actual=$(curl -s --max-time 3 -o - http://$HOST:$PORT/ | wc -c | tr -d ' ')
if [ "$declared" = "$actual" ]; then
    printf "  \033[32mPASS\033[0m  Content-Length matches body size (%s)\n" "$declared"
    PASS=$((PASS + 1))
else
    printf "  \033[31mFAIL\033[0m  Content-Length %s but body is %s bytes\n" "$declared" "$actual"
    FAIL=$((FAIL + 1))
fi

echo
echo "CONCURRENCY"
# Four clients at once; a single-threaded poll loop must serve them all.
ok=0
pids=""
for i in 1 2 3 4; do
    ( send 'GET / HTTP/1.1\r\nHost: x\r\n\r\n' > /tmp/webserv_c$i.txt ) &
    pids="$pids $!"
done
# wait on the client jobs only - a bare `wait` would also wait for the
# backgrounded server and hang forever.
wait $pids
for i in 1 2 3 4; do
    grep -q "HTTP/1.1 200" /tmp/webserv_c$i.txt && ok=$((ok + 1))
done
if [ "$ok" -eq 4 ]; then
    printf "  \033[32mPASS\033[0m  4 concurrent clients all served\n"
    PASS=$((PASS + 1))
else
    printf "  \033[31mFAIL\033[0m  only %s of 4 concurrent clients served\n" "$ok"
    FAIL=$((FAIL + 1))
fi

echo
echo "SURVIVAL"
# The server must still be alive after everything above.
if nc -z $HOST $PORT 2>/dev/null; then
    printf "  \033[32mPASS\033[0m  server still accepting connections\n"
    PASS=$((PASS + 1))
else
    printf "  \033[31mFAIL\033[0m  server died during the run\n"
    FAIL=$((FAIL + 1))
fi

# A client that vanishes mid-write must not kill the server (SIGPIPE).
(printf 'GET / HTTP/1.1\r\nHost: x\r\n\r\n' | nc -w 2 $HOST $PORT > /dev/null 2>&1 &) ; sleep 0.2
pkill -f "nc $HOST $PORT" 2>/dev/null
sleep 0.5
if nc -z $HOST $PORT 2>/dev/null; then
    printf "  \033[32mPASS\033[0m  survives a client disconnecting early\n"
    PASS=$((PASS + 1))
else
    printf "  \033[31mFAIL\033[0m  died on early client disconnect (SIGPIPE?)\n"
    FAIL=$((FAIL + 1))
fi

echo
echo "-----------------------------"
printf "  %s passed, %s failed\n" "$PASS" "$FAIL"
echo "-----------------------------"
echo
[ "$FAIL" -eq 0 ]
