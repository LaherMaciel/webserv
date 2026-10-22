# webserv — stress test report

Produced by the NEMESIS agent, 2026-10-15, against commit `1fb460b`
(`Laher_SandBox`, after merging `belinore_inprog`).

Scope: everything working end to end through the socket — event loop, connection
lifecycle, HTTP request parsing, CGI, uploads, DELETE. The configuration file
parser was attacked in a separate earlier session; all 8 findings from that run
are fixed.

Volume: ~140 hand-crafted inputs plus 1,200 randomised fuzz requests across two
configurations.

Findings 1, 2, 3 and 5 below were independently reproduced before this report was
written. The rest are as reported by the agent.

---

## How to re-run anything here

This machine has no `timeout(1)` and no `gtimeout`. Create a stand-in first,
otherwise every bounded command fails with exit 127 instead of giving a result:

```sh
mkdir -p /tmp/nem && cat > /tmp/nem/timeout <<'EOF'
#!/usr/bin/perl
my $t = shift; my $pid = fork();
if (!$pid) { exec @ARGV; exit 127; }
$SIG{ALRM} = sub { kill 'KILL', $pid; waitpid($pid, 0); exit 124; };
alarm $t; waitpid($pid, 0); exit($? >> 8);
EOF
chmod +x /tmp/nem/timeout && export PATH=/tmp/nem:$PATH
```

Exit codes: 0 accept, 1 clean reject, 2 non-std exception escaped, 124 hang,
139 SIGSEGV.

Do not start the server before running `tests/*.sh` — those scripts start and
kill their own instance. `pkill -f ./webserv` if one is stuck.

---

## CRITICAL

### 1. Request body is copied on every recv() — 16 MB upload reaches 1.03 GB RSS

`src/RequestParser.cpp:276` and `src/RequestParser.cpp:182`

Trigger: `POST` with `Content-Length: 16777216` and a 16 MB body.

Observed: returns 201, but server RSS goes from 1.5 MB to **1,043,616 KB** and
stays at 1.03 GB after the response completes. Streaming 49 MB drove peak
footprint to **3.0 GB**. Time is quadratic in body size:

| body | time |
|---|---|
| 4 MB | 0.12s |
| 8 MB | 0.42s |
| 16 MB | 1.69s |

Expected: RSS roughly equal to body size, linear time.

Cause: two whole-buffer copies per 4 KB read.
- `RequestParser.cpp:276` — `rawRequestLine_ = raw_request` copies the entire
  remaining buffer on every pass
- `RequestParser.cpp:182` — `std::string body = request.getBody()` copies the
  entire accumulated body on every pass
- compounded by `in_buffer_.erase(0, n)` at `src/Connection.cpp:219`

`/usr/bin/leaks` reports 0 leaks, so this is allocation churn, not a leak. It is
still a remote out-of-memory kill achievable with roughly 100 MB of legal traffic.

Fix direction: parse against the buffer by index instead of assigning
`rawRequestLine_`, and append to the body without reading it back.

### 2. Half-closed connection discards a complete request, no response, RST

`src/Server.cpp:207-211`

Trigger: send a full valid request, then `shutdown(SHUT_WR)` with no pause.
Equivalently `printf 'GET / HTTP/1.1\r\nHost: x\r\n\r\n' | nc -N 127.0.0.1 8080`.

Observed, reproduced 3/3 locally and 0/10 by the agent on both ports:

```
nc -N (FIN immediately)        -> NO RESPONSE, ECONNRESET
(printf ...; sleep 1) | nc     -> HTTP/1.1 200 OK
```

The agent isolated the threshold: 0.00s delay before FIN gives 0/5 responses,
0.05s delay gives 5/5.

Cause: the `POLLERR | POLLHUP | POLLNVAL` branch runs *before* the `POLLIN`
branch and `continue`s. When the FIN lands in the same 100 ms poll cycle as the
data, the connection is destroyed with the request still unread.

Why it matters: `nc -N` is a standard way to test a server by hand, and many
load generators close the write side immediately. To anyone using them the
server appears dead.

Fix direction: read and process pending input before honouring a hangup — check
`POLLIN` first, or treat `POLLHUP` as "no more data will arrive" rather than
"discard this connection now".

---

## HIGH

### 3. MAX_CONNECTIONS 10 — ten idle sockets are a total denial of service

`includes/webserv.hpp:5`, `src/Server.cpp:58-64`

Trigger: open 10 sockets, send one byte on each, leave them open, then make a
normal request.

Observed, reproduced locally: three legitimate clients all got `000` (no
response). The agent additionally saw the 11th and 12th clients accepted and
then closed with no status line, 3/3 trials, and 2 of 400 concurrent fast
clients getting RST with no response.

Expected: a far higher cap, and `503 Service Unavailable` rather than a silent
reset when full.

Note the missing idle timeout is separately known. What is new here is the cap
value of **10** and the silent RST.

### 4. Two HTTP responses in one reply (response splitting) on CGI POST

`src/Connection.cpp:169-174`, `src/ServerCgi.cpp:98-132`

Trigger: `POST` to a CGI script that writes headers and exits without reading
stdin, with a 1 MB body. Reproducible 3/3 at 1 MB, intermittent at 128 KB.

Observed: `HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n` immediately followed by
a complete `HTTP/1.1 500 Internal Server Error` plus body, in the same reply.

Cause: `queueResponse` does `out_buffer_ +=` with no check for an
already-queued response, and `ServerCgi` can take both the output-EOF success
branch (200) and the input-pipe `CGI_WRITE_ERROR` branch (500) for one request.

A guard in `queueResponse` would mask the symptom; the actual defect is two
completion paths firing for a single request.

---

## MEDIUM

### 5. default.conf uploads and DELETE are broken — wrong path

`config/default.conf:41`

```
config says:  upload_store ./www/uploads
on disk:      www/upload
```

Observed, reproduced locally: `POST /upload/t.txt` against `config/default.conf`
returns **500**, 3/3. `DELETE /upload/ok.txt` returns 404 and leaves the file.

This is a one-word fix in the config file, not a code defect. Uploads and DELETE
do work when `upload_store` names a directory that exists — which is why an
earlier manual check with a hand-written config reported them as working. That
earlier check was misleading: it validated the code against a config written to
match the disk, not against the project's own default config.

### 6. DELETE removes arbitrary files under root when the location has no upload_store

`src/Router.cpp:291-294`

Trigger: `location / { root <dir>; allowed_methods GET POST DELETE; }` then
`DELETE /index.html`.

Observed: 200, and the file the server serves is removed from disk.

Cause: DELETE falls back to `mapRootPath` instead of being confined to
`upload_store`. No traversal escape was found, but the entire serving root is
writable by any client.

This needs a decision, not just a patch: should DELETE be confined to
`upload_store`, or refused when `upload_store` is unset?

### 7. Path segment longer than 255 characters returns 500 instead of 404

`src/Router.cpp:46-56`

Trigger: `GET /` followed by 256 `a` characters. Observed 500, 3/3. 255
characters correctly returns 404.

Cause: `getPathInfo` maps only `EACCES`, `EPERM`, `ENOENT` and `ENOTDIR`;
`ENAMETOOLONG` falls through to `throw 500`.

---

## LOW

### 8. Bare-LF request is never answered and holds the fd forever

`src/RequestParser.cpp:144`

`GET / HTTP/1.0\n\n` returns 0 bytes and the descriptor is held indefinitely.
The parser only ever looks for `\r\n\r\n`, so a request using bare LF line
endings is treated as permanently incomplete. Combined with finding 3, a handful
of these is enough to exhaust the connection cap.

### 9. The 8192-byte header cap does not apply to complete requests

`src/RequestParser.cpp:147`

A 10 KB header value returns 200. The cap is only checked while `\r\n\r\n` is
absent, so it bounds incomplete requests but not complete oversized ones.

### 10. Unimplemented methods return 400 instead of 501

`src/RequestParser.cpp:20`

HEAD, PUT, PATCH, OPTIONS, TRACE, CONNECT and an invented BREW all return 400.
A syntactically valid method that is simply not implemented should be `501 Not
Implemented`; 400 claims the request was malformed, which it was not.

### 11. A legal chunked trailer is rejected

`src/RequestParser.cpp:259`

`0\r\nX-Trailer: v\r\n\r\n` returns 400. Trailers are legal after the terminating
zero-length chunk. The terminator check is anchored on exactly `0\r\n\r\n`.

### 12. Upload error statuses are all 500

`src/Router.cpp:229-235`

- POST over a read-only file returns 500, should be 403
- POST onto a directory returns 500, should be 409 or 403
- a 4000-character filename returns 500, should be 414

---

## What held

No crashes, leaks or hangs of any kind:

- **No crash** across 1,200 fuzzed requests (random methods, paths, versions,
  headers, bodies, split points, requests abandoned mid-stream) over two
  configurations. No segfault, no uncaught exception; both servers stayed alive
  and responsive throughout.
- **No fd leak.** Baseline of 8 descriptors restored after 2,000
  connect/disconnect cycles, 500 sequential GETs, 200 sequential CGI requests,
  400-way concurrency, CGI timeouts, CGI aborts and the fuzz runs.
- **No zombies**, no orphan CGI processes, checked immediately and 7 seconds
  after each run.
- **No memory leaks.** `/usr/bin/leaks` reports 0 leaks, 0 bytes.

Request framing:

- **Splitting at every boundary** — mid start line, mid header name, mid value,
  between and inside the CRLFs, one byte at a time — all 200.
- **Chunked at every boundary** — mid hex, after hex, inside the CRLF, mid data,
  before and inside the terminator, dribbled chunks, uppercase hex, a 64 KB
  chunk — all 200. Bad hex, negative sizes, `FFFFFFFFFFFFFFFF`, an LF
  terminator and size/data mismatch all return 400. `Transfer-Encoding: gzip`
  returns 501. Content-Length plus Transfer-Encoding returns 400. Negative and
  non-numeric Content-Length return 400.
- **Pipelining and keep-alive** — 100 pipelined GETs in one write produce 100
  correct responses. Errors mid-pipeline never desynchronise the stream
  (200,404,200 and 200,405,200 and 200,500,200 all correct). A POST body
  followed by a pipelined GET gives 201,200. `Connection: close` is honoured.
  HTTP/1.0 defaults are correct.

Security:

- **Path traversal held.** Raw `..` returns 400. `%2e%2e%2f` is never decoded,
  so it becomes a literal filename. Nothing was written or deleted outside the
  upload store or root in any trial. NUL and control bytes in the path return
  400. Malformed and truncated percent-encoding return 400.

CGI:

- Missing script 404, unreadable script 403, a silent script 500, exit 0 and
  exit 3 both 500, 2 MB of output 500 at the 1 MB cap, `Status: 999` and
  `Status: abc` both 500, duplicate and malformed CGI headers 500, a 1 MB POST
  body delivered intact, PATH_INFO works, client abort mid-CGI handled cleanly.
- **The 5-second CGI timeout works.** It fired at exactly 5s and returned 504 on
  three different hang shapes — a script sleeping 30s before any output, one
  that hangs forever after writing headers, and one that never writes anything.
  The connection recovered each time and the next request was served 200.

Flow control:

- **Backpressure held.** Requesting an 86 KB file and never reading it for 6
  seconds did not block the server: a second client was served 200 meanwhile,
  and all 86,050 bytes arrived intact afterwards.

Headers and methods:

- Absent Host on HTTP/1.1 returns 400 but is accepted on 1.0, duplicate Host
  returns 400, a header with no colon returns 400, and 2,000 headers plus a
  1 MB header were rejected without crashing.

DELETE semantics:

- Existing file 200, repeat 404, absent 404, a directory 403, no filename 400,
  `/` 403. A read-only file returns 200, which is correct POSIX behaviour since
  `unlink` requires write permission on the directory, not the file.

---

## Suggested order

Small and serious first:

1. **Finding 2**, half-closed connections — reorder the poll branches so pending
   input is read before a hangup is honoured. Smallest change with the largest
   visible effect.
2. **Finding 3**, the connection cap — raise `MAX_CONNECTIONS` and return 503
   instead of a silent reset.
3. **Finding 5**, the `default.conf` upload path — one word.

Then the ones that need a decision or real surgery:

4. **Finding 6**, DELETE scope — decide whether DELETE is confined to
   `upload_store` or refused without it.
5. **Finding 4**, response splitting — fix the two completion paths in
   `ServerCgi`, not just the symptom in `queueResponse`.
6. **Finding 1**, body copying — the largest piece of work, and the only one
   that lets a client kill the server outright.

Findings 7 through 12 are status-code and edge-case corrections, each small and
independent.
