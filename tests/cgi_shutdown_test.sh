#!/bin/bash

pkill -x webserv 2>/dev/null

HOST=${HOST:-127.0.0.1}
PORT=${PORT:-8080}
BIN=${BIN:-./webserv}
CONFIG=${1:-config/default.conf}
SERVER_LOG_FILE=${SERVER_LOG_FILE:-webserv.log}
TOKEN=$$
PID_FILE="/tmp/webserv-cgi-disconnect-${TOKEN}.pid"
LOG_FILE="/tmp/webserv-cgi-shutdown-${TOKEN}.log"
SERVER_PID=""
CURL_PID=""
CGI_PID=""

cleanup()
{
    if [ -n "$CURL_PID" ]; then
        kill "$CURL_PID" 2>/dev/null
        wait "$CURL_PID" 2>/dev/null
    fi
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
    fi
    if [ -n "$CGI_PID" ]; then
        kill "$CGI_PID" 2>/dev/null
    fi
    rm -f "$PID_FILE" "$LOG_FILE"
}

trap cleanup EXIT

LOG_START=0
if [ -f "$SERVER_LOG_FILE" ]; then
    LOG_START=$(wc -c < "$SERVER_LOG_FILE")
fi

"$BIN" "$CONFIG" > "$LOG_FILE" 2>&1 &
SERVER_PID=$!

for i in $(seq 1 50); do
    nc -z "$HOST" "$PORT" 2>/dev/null && break
    sleep 0.05
done

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "FAIL server did not start"
    exit 1
fi

curl -sS "http://${HOST}:${PORT}/cgi-bin/disconnect_test.py?token=${TOKEN}" >/dev/null 2>&1 &
CURL_PID=$!

for i in $(seq 1 50); do
    [ -f "$PID_FILE" ] && break
    sleep 0.05
done

if [ ! -f "$PID_FILE" ]; then
    echo "FAIL CGI child did not start"
    exit 1
fi

CGI_PID=$(cat "$PID_FILE")
if ! kill -0 "$CGI_PID" 2>/dev/null; then
    echo "FAIL CGI child exited before server shutdown"
    exit 1
fi

kill -TERM "$SERVER_PID"
wait "$SERVER_PID"
SERVER_PID=""

wait "$CURL_PID" 2>/dev/null
CURL_PID=""

if kill -0 "$CGI_PID" 2>/dev/null; then
    echo "FAIL CGI child ${CGI_PID} survived server shutdown"
    exit 1
fi
CGI_PID=""

if ! tail -c +$((LOG_START + 1)) "$SERVER_LOG_FILE" | grep -q "Server shutdown requested"; then
    echo "FAIL server did not leave the event loop cleanly"
    exit 1
fi

echo "PASS controlled shutdown terminated and reaped the active CGI child"
