#!/bin/sh

printf 'Content-Type: text/plain\r\n\r\n'
printf 'Hello from shell CGI\n'
printf 'Query: %s\n' "$QUERY_STRING"
