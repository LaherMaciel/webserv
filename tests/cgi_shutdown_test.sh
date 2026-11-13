#!/bin/bash

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
cd "$SCRIPT_DIR/.." || exit 1

HOST=${HOST:-127.0.0.1}
PORT=${PORT:-8080}
SERVER_LOG_FILE=${SERVER_LOG_FILE:-webserv.log}
TOKEN=$$
PID_FILE="/tmp/webserv-cgi-disconnect-${TOKEN}.pid"
SERVER_PID=${SERVER_PID:-}
CURL_PID=""
CGI_PID=""

cleanup()
{
    if [ -n "$CURL_PID" ]; then
        kill "$CURL_PID" 2>/dev/null
        wait "$CURL_PID" 2>/dev/null
    fi
    if [ -n "$CGI_PID" ]; then
        kill "$CGI_PID" 2>/dev/null
    fi
    rm -f "$PID_FILE"
}

trap cleanup EXIT

find_server_pid()
{
    if ! command -v lsof >/dev/null 2>&1; then
        return
    fi
    lsof -nP -iTCP:"$PORT" -sTCP:LISTEN -t 2>/dev/null | head -n 1
}

server_is_running()
{
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        return 1
    fi
    SERVER_STATE=$(ps -p "$SERVER_PID" -o stat= 2>/dev/null)
    case "$SERVER_STATE" in
        *Z*) return 1 ;;
    esac
    return 0
}

if ! curl -sS --connect-timeout 2 --max-time 2 \
    "http://${HOST}:${PORT}/" >/dev/null; then
    echo "FAIL server is not reachable at ${HOST}:${PORT}"
    exit 1
fi

if [ -z "$SERVER_PID" ]; then
    SERVER_PID=$(find_server_pid)
fi

if [ -z "$SERVER_PID" ]; then
    echo "FAIL could not find the running server PID; install lsof or set SERVER_PID"
    exit 1
fi

PROCESS_NAME=$(ps -p "$SERVER_PID" -o comm= 2>/dev/null)
case "$PROCESS_NAME" in
    *webserv*) ;;
    *)
        echo "FAIL process ${SERVER_PID} listening on port ${PORT} is not webserv"
        exit 1
        ;;
esac

echo "Using running webserv PID ${SERVER_PID} on ${HOST}:${PORT}"
echo "This test will terminate that server with SIGTERM."

LOG_START=0
if [ -f "$SERVER_LOG_FILE" ]; then
    LOG_START=$(wc -c < "$SERVER_LOG_FILE")
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

if ! kill -TERM "$SERVER_PID"; then
    echo "FAIL could not send SIGTERM to server ${SERVER_PID}"
    exit 1
fi

for i in $(seq 1 100); do
    if ! server_is_running; then
        break
    fi
    sleep 0.05
done

if server_is_running; then
    echo "FAIL server ${SERVER_PID} did not exit after SIGTERM"
    exit 1
fi

wait "$CURL_PID" 2>/dev/null
CURL_PID=""

for i in $(seq 1 40); do
    if ! kill -0 "$CGI_PID" 2>/dev/null; then
        break
    fi
    sleep 0.05
done

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
