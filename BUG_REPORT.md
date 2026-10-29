# webserv — Bug and Fix Report

**Branch:** `Laher_SandBox` (base commit `a23336a`, plus the uncommitted fixes listed below)
**Platform tested:** macOS (Darwin 25.5.0), arm64
**Date:** 2026-10-27
**Config under test:** `config/default.conf` — `:8080` with two virtual hosts (`localhost`/`webserv.local` default, `other.local`), `:8081` minimal with `client_max_body_size 100`

Testing was done against the running binary with scripted HTTP clients: roughly 90,000 requests
across malformed input, path traversal, body limits, routing, keep-alive, pipelining, CGI,
concurrency and resource exhaustion. Every finding below was reproduced directly, not inferred
from reading the code.

---

## Summary

| ID | Problem | Severity | Status |
|----|---------|----------|--------|
| B1 | Half-close drops the response entirely | High | **FIXED** |
| B2 | Silent-client DoS — Slowloris guard never fires | High | **FIXED** |
| B3 | No percent-decoding of the request target | Medium | **FIXED** |
| B4 | Chunked terminator rejects RFC-legal requests | Medium | **FIXED** |
| B5 | Chunk sizes parsed too leniently | Low | **FIXED** |
| B6 | Header size ceiling is 12288, not 8192 | Low | **FIXED** |
| B7 | `headersStart_` never initialised in the constructor | Low (latent) | **FIXED** (B2 edit 2) |
| B8 | Dead declarations left by the merge | Cosmetic | **FIXED** |
| B9 | `www/private/` missing, so config advertises an untestable case | Cosmetic | **FIXED** |
| B10 | Autoindex fixtures exist only on `main` | Housekeeping | **FIXED** |
| N1 | Unbounded `in_buffer_` on the chunked framing path — `client_max_body_size` bypassed | **High** | **OPEN** |
| N2 | Startup warning claims multiple ports are not wired up, but they are | Cosmetic | **OPEN** |

---

## B1 — Half-close drops the response entirely  **[FIXED]**

**Severity:** High. The server returned nothing at all to a well-formed request.

### Reproduction

```python
s = socket.create_connection(("127.0.0.1", 8080))
s.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")
s.shutdown(socket.SHUT_WR)        # client closes its write side only
s.recv(65536)
```

**Observed (before fix):** `NO RESPONSE`, 0 bytes, connection closed.
**Expected:** `200 OK` with the full body.

### Cause

macOS `poll()` is implemented over kqueue, so `POLLHUP` is raised by EOF on the **read** filter.
A peer half-close therefore arrives as `revents = POLLIN|POLLHUP` (`0x11`) with **`POLLOUT`
suppressed**, even though the socket is still perfectly writable.

In `Server::handleConnectionEvent` the consequence was:

1. The `POLLIN` branch ran, routed the request, queued the response and set `events = POLLOUT`.
2. `if (revents & POLLOUT)` was **false** for that iteration, so nothing was sent.
3. `if (revents & POLLHUP) return CLOSE_CONNECTION;` closed the socket with the serialized
   response still sitting in `out_buffer_`.

Responsible line: `src/Server.cpp:350`.

### Fix applied

```cpp
if ((revents & POLLHUP) && !conn->hasPendingResponse())
    return CLOSE_CONNECTION;
return CONTINUE_CONNECTION;
```

The hangup is only honoured when there is nothing left to send. With a response queued the
connection survives the iteration; because `processBufferedRequest` already set
`events = POLLOUT`, the next `poll()` no longer requests `POLLIN`, so no `POLLHUP` is reported
and `POLLOUT` comes through normally — the existing send branch delivers the response.

### Verification

```
half-close after request      HTTP/1.1 200 OK  (391 bytes)
normal (control)             HTTP/1.1 200 OK  (391 bytes)
```

Byte-identical to the control. Note this bug is macOS-specific: on Linux `POLLHUP` means a full
hangup and `POLLOUT` keeps being reported, so the original code already worked there. The fix is
a no-op on Linux and correct on both.

---

## B2 — Silent-client DoS: the Slowloris guard never fires  **[FIXED]**

**Severity:** High. 140 idle sockets deny service to every client for 30 seconds.

### Reproduction

Open 140 TCP connections to `:8080` and send **zero bytes**. Hold them open. Then make a normal
request from another client.

**Observed:**

```
    0.0s  HTTP/1.1 503 Service Unavailable
    3.0s  HTTP/1.1 503 Service Unavailable
   ...
   27.1s  HTTP/1.1 503 Service Unavailable
   30.1s  HTTP/1.1 200 OK   <-- recovered after 30s of outage
```

Server log over the same window: **0** `header deadline expired`, **128** `idle timeout`.

**Expected:** `CLIENT_HEADER_DEADLINE_SECONDS` (10s) should reap a client that has sent nothing,
limiting the outage to ~10s — or preventing it entirely.

### Cause

A connection that has sent nothing never leaves the state the deadline requires:

| Location | Code | Effect |
|---|---|---|
| `src/Connection.cpp:16` | `state_(CONN_WAITING_FOR_REQUEST)` | initial state on accept |
| `src/Connection.cpp:245-246` | `state_ = CONN_READING_HEADERS;` `headersStart_ = time(NULL);` | set **only** inside `handleRequest()` |
| `src/Server.cpp:320` | `if (conn->readFromSocket() == -1)` | `handleRequest()` runs only after bytes arrive |
| `src/Connection.cpp:67` | `if (state_ != CONN_READING_HEADERS) return false;` | deadline inert in any other state |

So the guard protects a client that starts sending headers and stalls mid-way, but not the
connect-and-stay-silent variant. The comment at `src/Connection.cpp:64` claims it stops
Slowloris; it only stops one form of it.

The server does degrade gracefully — the soft connection limit answers `503` rather than
resetting — but it is unavailable for real work until the 30s idle timeout drains the sockets.

### Fix

The obvious one-line change (adding `CONN_WAITING_FOR_REQUEST` to the deadline) is **wrong**: it
would regress keep-alive. `resetForNewRequest()` (`src/Connection.cpp:50`) puts established idle
connections into that same state, and a legitimate client idling between requests must get the
30s idle timeout, not the 10s header deadline. This was verified — a client making 4 requests
with 12s gaps stays alive today and must continue to.

A brand-new connection therefore needs to be distinguishable from one sitting between requests.
Four edits:

**1. `includes/Connection.hpp:28`** — new state, first in the enum:

```cpp
enum ConnectionState
{
    CONN_NEW,
    CONN_WAITING_FOR_REQUEST,
```

**2. `src/Connection.cpp:16`** — initialise `headersStart_` and start in `CONN_NEW`. The init-list
order must be `lastActivity_`, `headersStart_`, `state_` to match the declaration order in
`includes/Connection.hpp:92-94`, or `-Wreorder-ctor` under `-Werror` fails the build:

```cpp
                    , lastActivity_(std::time(NULL)), headersStart_(std::time(NULL))
                    , state_(CONN_NEW) {}
```

**3. `src/Connection.cpp:243`** — a new connection must also be able to enter header-reading:

```cpp
            if (state_ == CONN_NEW || state_ == CONN_WAITING_FOR_REQUEST)
```

**4. `src/Connection.cpp:67`** — the deadline now covers both states:

```cpp
    if (state_ != CONN_NEW && state_ != CONN_READING_HEADERS)
        return false;
```

Edit 2 also closes **B7** — that is why the two cannot be done separately.

Resulting behaviour: a silent socket gets 10s, a client stalled mid-header gets 10s, and an
established keep-alive connection still gets 30s, because edit 4 deliberately leaves
`CONN_WAITING_FOR_REQUEST` out. `CONN_WAITING_FOR_REQUEST` is compared in only one place
(`src/Connection.cpp:243`), so adding a state is low risk.

This touches the connection state machine, so it should be agreed with its author.

### Verification

Applied and measured. 140 silent sockets, then a normal request every 2s from another client:

```
    0.0s  HTTP/1.1 503 Service Unavailable
    2.0s  HTTP/1.1 503 Service Unavailable
    4.0s  HTTP/1.1 503 Service Unavailable
    6.0s  HTTP/1.1 503 Service Unavailable
    8.0s  HTTP/1.1 503 Service Unavailable
   10.0s  HTTP/1.1 200 OK   <-- recovered
```

Outage **30.1s -> 10.0s**. The server log over the same window inverted exactly as predicted:

| | Before | After |
|---|---|---|
| `header deadline expired` | 0 | **128** |
| `idle timeout` | 128 | **0** |

The silent sockets are now reaped by the 10s header deadline rather than drifting to the 30s
idle timeout.

Keep-alive regression test — 4 requests on one socket with 12s gaps, which is the failure this
fix could plausibly have caused:

```
   0.0s req 1: HTTP/1.1 200 OK
  12.0s req 2: HTTP/1.1 200 OK
  24.0s req 3: HTTP/1.1 200 OK
  36.0s req 4: HTTP/1.1 200 OK
```

All 4 answered on the same connection across 36s elapsed — well past the 10s deadline, so
`resetForNewRequest()`'s `CONN_WAITING_FOR_REQUEST` is correctly excluded from it.

Smoke test, no regressions: `/` 200, `/nope` 404, `/static/` 200, `/old-page` 301, 1.5MB POST to
`/cgi-bin` 413, 3 pipelined requests -> 3 responses.

---

## B3 — No percent-decoding of the request target  **[FIXED]**

**Severity:** Medium. Autoindex generates links the server cannot serve, and the subject
requires browser testing.

### Reproduction

With `www/static/autoindex-test/` present (see B10):

```
GET /static/autoindex-test/a%20%26%20b.txt   -> 404 Not Found
GET /static/autoindex-test/a & b.txt         -> 400 Bad Request
GET /static/autoindex-test/alpha.txt         -> 200 OK      (no special chars)
```

**Expected:** the encoded form decodes to `a & b.txt`, `stat()` finds it, 200 OK.

### Cause

The request target is never percent-decoded. `RequestParser` stores it raw and
`Router::mapRootPath` passes it to the filesystem literally, so the server looks for a file whose
name contains the characters `%`, `2`, `0`. Every file with a space or special character is
unreachable through the link autoindex itself produced.

### ⚠ Sequencing warning

The absence of decoding is also *why* path traversal is currently safe: `%2e%2e%2f` is treated as
a literal filename and never becomes `../`. Seven traversal tests pass today for this reason, and
no upload or delete was able to escape its configured root.

**Decode before `validateUrlPath()`, never after.** Decoding downstream of validation turns
`%2e%2e%2f` into `../` after the check has already passed, which converts this cosmetic bug into
a directory-traversal vulnerability.

### Fix applied

A `percentDecode()` helper was added to `RequestParser` (declared at
`includes/RequestParser.hpp:39`, defined at `src/RequestParser.cpp:68`) and called from
`parseRequestLine()`:

```cpp
    std::string query = parseQuery(requestTarget);
    requestTarget = percentDecode(requestTarget);
    validateRequestLine(method, requestTarget, version);
```

Three things about that placement, each deliberate:

- **After `parseQuery()`** — the query string is split off first and left raw, because CGI's
  `QUERY_STRING` must carry the undecoded value. A `%26` inside a parameter value is therefore
  never mistaken for the `&` separator.
- **Before `validateRequestLine()`**, and so before `Router::validateUrlPath()` — this is the
  ordering the warning above demands. `%2e%2e%2f` becomes `../` *upstream* of the segment scan at
  `src/Router.cpp:48`, which then rejects it with 400.
- **After `validateRequestTarget()`** (`src/RequestParser.cpp:81`), which has already proven every
  `%` is followed by two hex digits. The decoder can rely on that and does not re-validate.

`%00` is rejected with 400 rather than decoded — a NUL byte in a path is a filesystem attack
(truncation), never a legitimate filename.

### Verification

```
/static/autoindex-test/a%20%26%20b.txt      200 OK      (was 404)
/static/autoindex-test/quote%22test.txt     200 OK      (was 404)
/static/autoindex-test/%3Cscript%3E.txt     200 OK      (was 404)
/st%61tic/autoindex-test/alpha.txt          200 OK      (decoding also works mid-path)
/static/alpha%00.txt                        400 Bad Request
```

**Traversal regression suite — all 8 still rejected with 400:**

```
/../etc/passwd                                      400
/..%2fetc/passwd                                    400
/%2e%2e%2fetc/passwd                                400
/static/%2e%2e%2f%2e%2e%2f%2e%2e%2fetc/passwd       400
/static/..%2F..%2F..%2Fetc%2Fpasswd                 400
/%2E%2E/%2E%2E/etc/passwd                           400   (uppercase hex)
/static/subdir/%2e%2e/%2e%2e/%2e%2e/etc/passwd      400
/static/../../etc/passwd                            400   (literal, unchanged)
```

The encoded forms now *do* decode to `../` — and are then caught by `validateUrlPath()` instead of
being treated as literal filenames. The defence moved from accidental to intentional, which is
the answer to give if a grader asks why decoding is safe here.

---

## B4 — Chunked terminator rejects RFC-legal requests  **[FIXED]**

**Severity:** Medium. A conformant client is rejected with 400.

### Reproduction

`POST /upload/f.bin` on `:8080` with `Transfer-Encoding: chunked`:

| Last chunk written as | Observed | Expected |
|---|---|---|
| `0\r\n\r\n` | 201 Created | 201 |
| `00\r\n\r\n` | **400 Bad Request** | 201 — RFC 9112 allows `1*"0"` |
| `0\r\nX-Sum: abc\r\n\r\n` | **400 Bad Request** | 201 — trailers are legal (RFC 9112 §7.1.2) |
| `0;x=y\r\n\r\n` | **400 Bad Request** | 201 — extensions are legal on any chunk |
| `5;foo=bar\r\n...` | 201 Created | 201 (mid-chunk extensions already work) |

### Cause

`src/RequestParser.cpp:264`: (line numbers in the Cause sections refer to the code before these fixes)

```cpp
if (str.find("0\r\n\r\n") != 0)
    throw 400;
```

This demands the terminator be exactly that 5-byte literal. The parser needs to accept one or
more `0` digits, an optional `;`-prefixed extension to end of line, and then either `\r\n` or a
trailer section terminated by a blank line.

Worth noting what already works and should not be disturbed: normal chunked bodies, mid-chunk
extensions, byte-by-byte dribble, and splits at every chunk boundary all pass. Only the last
chunk is too rigid.

### Fix applied

The rigid literal was the visible half of the bug. The other half was upstream: `getChunkIndex()`
validates every chunk the same way, so for the last chunk it also demanded a `\r\n` immediately
after the size line (`src/RequestParser.cpp:234`). A trailer section failed there, *before* line
264 was ever reached. Both halves had to change.

**1. `src/RequestParser.cpp:267`** — the last chunk carries no data, so the data checks are skipped:

```cpp
    if (bodySize + n > maxBodySize_)
            throw 413;
    if (n == 0)
        return (0);
    if (str.size() < endline + 2 + n + 2)
```

**2. `src/RequestParser.cpp:305`** — the terminator is now found rather than matched literally:

```cpp
    size_t trailerEnd = str.find("\r\n\r\n");
    if (trailerEnd == std::string::npos)
    {
        std::cout << "The trailer section is incomplete." << std::endl;
        return (-1);
    }
    endOfBody_ += trailerEnd + 4;
    str.erase(0, trailerEnd + 4);
```

The first blank line after the last chunk's size line ends the trailer section, which is true
whether there are no trailers (`0\r\n\r\n`), one, or several. The `npos` branch returns `-1`
instead of throwing, so a terminator still arriving over several `recv` calls is waited for rather
than rejected — the same incomplete-data contract the rest of the function uses. The `1*"0"` and
`;ext` forms are accepted as a side effect, because the size line is parsed by the digit scan from
B5 rather than compared against a literal.

### Verification

```
0\r\n\r\n                      201 Created
00\r\n\r\n                     201 Created   (was 400)
0000\r\n\r\n                   201 Created   (was 400)
0\r\nX-Sum: abc\r\n\r\n         201 Created   (was 400)
0\r\nX-A: 1\r\nX-B: 2\r\n\r\n   201 Created   (was 400)
0;x=y\r\n\r\n                  201 Created   (was 400)
6;foo=bar\r\nhello \r\n0\r\n\r\n 201 Created   (mid-chunk extension, unchanged)
multi-chunk body                 201 Created   (unchanged)
empty body, terminator only      201 Created   (unchanged)
```

---

## B5 — Chunk sizes parsed too leniently  **[FIXED]**

**Severity:** Low. Opposite direction from B4.

### Reproduction

```
chunk size "+5"    -> 201 Created   (expected 400)
chunk size "0x5"   -> 201 Created   (expected 400)
chunk size " 5"    -> 201 Created   (expected 400)
```

### Cause

`src/RequestParser.cpp:220` uses `std::strtol(hex.c_str(), &end, 16)`, which silently accepts a
leading `+`/`-`, a `0x` prefix and leading whitespace. A chunk size is strictly `1*HEXDIG`, so
these should be rejected before `strtol` is consulted.

### Fix applied

`src/RequestParser.cpp:250` — every character up to the optional `;` is required to be a hex
digit, and at least one must be present, *before* `strtol` is trusted:

```cpp
    hex = str.substr(0, endline);
    size_t digits = 0;
    while (digits < hex.size() && hex[digits] != ';')
    {
        if (!std::isxdigit(static_cast<unsigned char>(hex[digits])))
            throw 400;
        digits++;
    }
    if (digits == 0)
        throw 400;
    long value = std::strtol(hex.c_str(), &end, 16);
```

The scan stops at `;` so chunk extensions keep working. `0x5` is caught by the `x`, `+5`/`-5` and
` 5` by their first character, and an empty size line by `digits == 0`. `<cctype>` was added to the
includes for `std::isxdigit`. The existing `value == LONG_MAX` overflow guard is untouched.

### Verification

```
size "+5"    400 Bad Request   (was 201)
size "0x5"   400 Bad Request   (was 201)
size " 5"    400 Bad Request   (was 201)
size "-5"    400 Bad Request
size ""      400 Bad Request
size "zz"    400 Bad Request
size "5"     201 Created       (still accepted)
size "A"     201 Created       (10 bytes — hex still parsed as hex)
```

---

## B6 — Header size ceiling is 12288, not 8192  **[FIXED]**

**Severity:** Low. Bounded, not exploitable.

### Reproduction

```
header value   8000 bytes -> 200 OK
header value   9000 bytes -> 200 OK      (over MAX_HEADER_SIZE 8192)
header value  16000 bytes -> 431 Request Header Fields Too Large
```

### Cause

`src/RequestParser.cpp:148` only tests the size when `\r\n\r\n` has **not** been found:

```cpp
size_t headersEnd = raw_request.find("\r\n\r\n");
if (headersEnd == std::string::npos)
{
    if (raw_request.size() > MAX_HEADER_SIZE)
        throw 431;
    ...
}
```

A header block that completes within the next `recv` is parsed without the check, so the real
ceiling is `MAX_HEADER_SIZE + IO_CHUNK_SIZE` = `8192 + 4096` = **12288** bytes. Above that, 431
fires correctly and memory stays bounded, so this is a precision issue rather than a DoS. To make
it exact, also validate the header length after `headersEnd` is found.

### Fix applied

`src/RequestParser.cpp:181` — the size is now also checked once the end of the header block is
known, immediately after the `npos` branch:

```cpp
    if (headersEnd + 4 > MAX_HEADER_SIZE)
        throw 431;
    size_t requestLineEnd = raw_request.find("\r\n");
```

`headersEnd` is the offset of the `\r\n\r\n`, so `headersEnd + 4` is the full size of the header
block including its terminator — which is what `MAX_HEADER_SIZE` is meant to bound. The two checks
now cover both cases: the old one rejects a block that is still growing, the new one rejects a
complete block that arrived inside one `recv`.

### Verification

```
header value  4000 bytes  200 OK
header value  7900 bytes  200 OK
header value  9000 bytes  431 Request Header Fields Too Large   (was 200)
header value 16000 bytes  431 Request Header Fields Too Large
```

The ceiling is now `MAX_HEADER_SIZE` exactly, independent of `IO_CHUNK_SIZE`.

---

## B7 — `headersStart_` never initialised in the constructor  **[FIXED]**

**Severity:** Was low in isolation, but it was a trap for the B2 fix.

`src/Connection.cpp:12-16` did not initialise `headersStart_`; it was first assigned at
`src/Connection.cpp:246`, inside `handleRequest()`. Nothing read it before then only because
`headerDeadlineExpired()` short-circuited on the state check first (`src/Connection.cpp:67`).

Widening that state check is exactly what B2 required, which would have read an uninitialised
`std::time_t` — so the two had to be fixed in one change.

**Fix applied:** `headersStart_(std::time(NULL))` added to the member init list at
`src/Connection.cpp:16`, placed between `lastActivity_` and `state_` to match the declaration
order in `includes/Connection.hpp:92-94` and satisfy `-Wreorder-ctor` under `-Werror`. This is
B2 edit 2; see B2's verification.

---

## B8 — Dead declarations left by the merge  **[FIXED]**

`includes/Connection.hpp:63` and `:67` still declare:

```cpp
bool            cgiIsRunning() const;
void            resetRequest();
```

Neither has a definition anywhere, and neither is called. Harmless while unused — but the moment
something calls one you get an undefined-symbol **link** error rather than a compile error, which
is the same trap that `Router::contentType` produced after an earlier merge. Delete both.

**Fix applied:** both declarations removed from `includes/Connection.hpp`. The project rebuilds
clean, confirming nothing referenced them.

---

## B9 — `www/private/` missing, config advertises an untestable case  **[FIXED]**

`config/default.conf:60-64` declares a location whose stated purpose is "a directory with no
index and no autoindex -> 403", but `www/private/` does not exist on disk, so the request returns
**404** instead. The server is behaving correctly; the fixture is absent.

Creating `www/private/` with one file inside makes the config's own checklist honest and turns
this into a real 403 test.

**Fix applied:** `www/private/secret.txt` created.

```
/private/            403 Forbidden   (was 404 — the config's stated case is now real)
/private/secret.txt  200 OK          (the file itself is still served)
```

---

## B10 — Autoindex fixtures exist only on `main`  **[FIXED]**

`www/static/autoindex-test/` was committed in `a23336a` on `main`, which `Laher_SandBox` does not
contain. Checking out the sandbox therefore deletes the directory, and every request under it
returns 404 — which can easily be mistaken for a server regression.

Fast-forward `main -> Laher_SandBox` so both branches carry the fixtures. They are needed for the
autoindex HTML-escaping test (filenames `<script>.txt`, `a & b.txt`, `quote"test.txt`), which
passes and should stay runnable on either branch.

**Fix applied:** `git merge --ff-only main` on `Laher_SandBox` (`ce8a96b..a23336a`). Both branches
now carry the fixtures, and the escaping test passes on the working branch:
`&lt;script&gt;` and `a &amp; b` both present in the generated listing. The fixtures also became
the test data for B3, since their filenames are exactly the ones that need percent-decoding.

---

## N1 — Unbounded `in_buffer_` on the chunked framing path

**Severity: High — OPEN.** One connection drives RSS to over 1 GB. Found by NEMESIS in the new
B4/B5 code, reproduced independently here.

### Reproduction

Two variants, both against **`:8081`, whose `client_max_body_size` is 100 bytes**:

```
POST / HTTP/1.1
Host: localhost
Transfer-Encoding: chunked

(then stream bytes that never contain a CRLF — an unterminated chunk-size line)
```

```
POST / HTTP/1.1
Host: localhost
Transfer-Encoding: chunked

5\r\nhello\r\n0\r\n
(then stream "X-Pad: aaa…\r\n" trailer lines forever, never a blank line)
```

**Observed:**

| Variant | Bytes sent | Peak RSS | Elapsed |
|---|---|---|---|
| Unterminated chunk-size line | 20 MB | **1141.6 MB** | 6.2s |
| Never-terminating trailer section | 20 MB | **1091.2 MB** | 6.2s |

Roughly **57x amplification** from `std::string` reallocation, on a server configured to accept
100-byte bodies. `client_max_body_size` provides **zero** protection.

**Expected:** the connection is refused (413 or 400) once accumulated unparsed bytes exceed what
any configured limit could possibly need.

### Cause

Both variants land on an incomplete-data `return (-1)` that is taken *before* any size check runs:

| Location | Code | Effect |
|---|---|---|
| `src/RequestParser.cpp:243-248` | `endline == npos` → `return (-1)` | returns before the `bodySize + n > maxBodySize_` check at line 265 |
| `src/RequestParser.cpp:305-310` | `trailerEnd == npos` → `return (-1)` | returns with `endOfBody_` unchanged |
| `src/Connection.cpp:256-260` | `in_buffer_.erase(0, parser_.endOfBody_)` | erases **0** bytes; body incomplete → `WAIT_FOR_MORE` |
| `src/Connection.cpp:174` | `in_buffer_.append(...)` | appends the next `recv` forever; nothing bounds `in_buffer_.size()` |

**`client_max_body_size` only bounds decoded *body* bytes. It does not bound *framing* bytes** —
the chunk-size line, chunk extensions, or the trailer section — and there is no independent cap on
`in_buffer_`.

Two further observations from reproducing it:

- **No timeout ever fires.** 0 header-deadline hits and 0 idle-timeout hits across the whole
  attack, because `refreshActivity()` (`src/Connection.cpp:62`) runs on every successful `recv`.
  The attacker is *busy*, not idle, so the B2 fix does not touch this. That is what makes it hold
  indefinitely instead of self-limiting.
- **It is a transient spike, not a leak.** RSS returns to ~2.4 MB once the connection is reaped,
  fds 9 → 9, and `leaks` reports 0 leaks / 0 bytes. Still a denial of service.

### The contrast that isolates it

The `Content-Length` path is **correct**: a declared `Content-Length: 50000000` with no body sent
returns 413 immediately with RSS flat at 1648 KB, because the check at `src/RequestParser.cpp:207`
runs before allocation. Only the chunked framing path is unbounded.

### Fix direction (not yet applied)

The cap must live in `Connection`, **where the bytes accumulate** — not in `RequestParser`. The
parser takes its `return (-1)` before any limit check executes, so a check added inside
`getChunkIndex` or `copyByChunks` is unreachable on these inputs. This is the same reachability
mistake made during the original chunked 413 work: a correct check placed after an early return
never runs.

Suggested shape: once the body is incomplete and `in_buffer_.size()` exceeds
`listener_->maxBodySizeLimit_` plus a small framing allowance, reject with 413 and close.

---

## N2 — Startup warning claims multiple ports are not wired up, when they are

**Severity: Cosmetic — OPEN.** But it misrepresents the project to anyone reading the terminal.

`src/main.cpp:52-56` prints, for any config with more than one server block:

```
warning: 2 server block(s) ignored, multiple ports not wired up yet
```

This is stale text from before `Listener` existed. All three server blocks in
`config/default.conf` are demonstrably live:

```
server 1   :8080  Host: localhost        200
server 2   :8080  Host: other.local      404   (distinct vhost, own root)
server 3   :8081  GET /                  200
server 3   :8081  POST 200 bytes         413   (its own client_max_body_size 100 enforced)

listening sockets: 127.0.0.1:8080, 127.0.0.1:8081
```

Nothing is ignored. The warning should be deleted — it invites exactly the wrong conclusion at
defence, and it already misled NEMESIS, which recorded "only configs[0] is served" as a known gap
on the strength of this message alone.

---

## What was verified as working

Not every result is a bug. The following all passed and are worth knowing are solid:

**Security**
- Path traversal: `/../Makefile`, `/../../etc/passwd`, `/..%2f..%2fetc%2fpasswd`,
  `/www/../../Makefile`, `/./././/Makefile`, `//etc/passwd`, absolute URI `http://evil.com/` —
  all blocked (400/403/404).
- Autoindex HTML escaping: no raw `<script>` in output; `&lt;`, `&quot;`, `&amp;` all correct. No XSS.
- Upload and DELETE cannot write or remove outside the configured root.

**Request handling**
- 14 of 15 malformed-request classes rejected correctly: garbage bytes, missing Host on HTTP/1.1,
  unknown method, bad and missing version, space in path, negative and non-numeric
  `Content-Length`, `Content-Length` + `Transfer-Encoding` together, bad transfer coding (→501),
  bad chunk hex, chunk size/data mismatch, header line without a colon.
- Keep-alive: 2 pipelined requests in one write → 2 responses; 5 sequential on one socket;
  `Connection: close` honoured.
- Virtual hosts: by name, unknown name → default server, Host case-insensitive, Host with port.

**Body limits (`client_max_body_size`)**
- `Content-Length` over the limit → 413 **before** allocating: a declared `2000000000` is refused
  with RSS staying at 1.0 MB.
- Chunked over the limit caught by accumulation; exactly at the limit accepted (no off-by-one).
- A 1 GB chunk announced with zero payload bytes sent → 413 on the chunk header alone.
- Per-location override works upward: the same 1.5 MB body gives 413 on `/cgi-bin`
  (limit 1000000) and 201 Created on `/upload` (limit 5000000).
- Every 413 carries `Connection: close` and produces exactly one response — no second spurious
  400 from the unread body tail.

**Limits and timeouts**
- Connection cap: 150 simultaneous → 128 served, next 22 answered `503 Service Unavailable`,
  none dropped silently.
- Header deadline closes a stalled mid-header client at 9.4s (limit 10s).
- Idle timeout closes a silent connection at 29.6s (limit 30s).
- Keep-alive activity refresh works: 4 requests over 36s elapsed, all 200 OK.

**Robustness**
- 200 concurrent mixed requests (static GETs, autoindex, 200 KB uploads, oversized POSTs, 404s)
  completed in 0.6s with every response correct and zero errors.
- No file-descriptor leak: 9 fds before, 9 after.
- No memory leak: 1.8 MB RSS before, 2.2 MB after a 1.5 MB POST, a declared 2 GB and ~350
  connections.
- No crash, no hang, no unreaped CGI children across roughly 90,000 requests.

### Second adversarial pass (after all ten fixes)

A second full attack run was made against the fixed build. It produced exactly one new finding
(N1) plus the stale warning (N2). Everything below passed.

**Percent-decoding vs traversal — 19/19 blocked.** Beyond the 8 already listed: uppercase and
mixed-case hex (`%2E%2E%2F`, `%2e%2E%2f`), UTF-8 overlong encodings of `/` and `..`
(`%c0%af`, `%c0%ae`, `%e0%80%af`), the IIS `%uff0e` form, and `/static/....//`.

**Double encoding is not decoded twice** — `%252e%252e%252f` → 404 and
`/%25%32%65%25%32%65%25%32%66` → 404. A single decode pass means `%25` yields a literal `%` and
stops there. This is the property that makes the decoder safe; a second pass anywhere downstream
would reopen the traversal hole.

**`%2f` does become a real separator**, and still cannot escape:

```
/static%2fautoindex-test%2falpha.txt   200   decoded '/' serves the file
/static%2f..%2fMakefile                400   but traversal is still caught
/%2fstatic/autoindex-test/alpha.txt    200   '//' collapses, stays inside www/
```

A decoded `/` does **not** create segments the validator already walked past — because
`percentDecode()` runs upstream of segmentation. That ordering is load-bearing. (Note this is a
deliberate deviation from nginx, which rejects `%2f` in a path by default.)

**Encoded NUL, control characters and `%25`:** `%00`, `%0d`, `%0a`, `%09`, `%7f`, `%80`, `%ff`
all 400. `%20` decodes and 404s honestly. `%25` → literal `%`, 404.

**Host header is correctly not decoded** — `Host: local%2ehost` and `Host: localhost%00` both
resolve to the default server with no crash and no decode.

**Query string correctly left raw** — `?x=%2e%2e%2f%2e%2e%2fetc%2fpasswd` is not treated as a
path, and `QUERY_STRING` carries the raw value to CGI.

**Chunked parsing (the new B4/B5 code).** Trailers split across `recv` boundaries, a half-written
extension completed later, and `\r\n\r\n` inside a trailer value all handled. Overflow is
covered: chunk size `ffffffffffffffff` (SIZE_MAX), `7fffffffffffffff` (LONG_MAX) and a 17-hex-digit
size all 400; an oversized announced chunk 413 on the header alone. The only unbounded chunked
path is N1.

**The `CONN_NEW` state machine under mixed load** — 60 silent sockets and 5 keep-alive clients
concurrently: silent sockets reaped at 10.0s, all 5 keep-alive clients completed 6 requests each
across the same window with zero failures and none killed by the header deadline, and a brand-new
client was served 200 during the attack. The 10s/30s split holds in both directions
simultaneously.

**Header ceiling is exact** — 8191 → 200, 8192 → 200, 8193 → 431. 900 small headers totalling
over 8192 → 431, so many-small is bounded by the same ceiling as one-huge. Duplicate headers 400.

**CGI timeouts** — `CGI_TIMEOUT_SECONDS` is 5 (`includes/CgiProcess.hpp:13`). A script sleeping 60s
returns 504 Gateway Timeout with first byte at 4.55s, reproducible, no orphan interpreter, fds
8 → 8, server still serving afterwards. Two clients dropped mid-CGI left no zombies and no fd leak.

**Resources** — 200 concurrent mixed requests in 0.61s with 0 mismatches; 300 sequential
connections fds 8 → 8; `leaks` 0 leaks / 0 bytes on both the live process and the bad-config error
path (`./webserv /nonexistent.conf`).

**One conformant-but-worth-knowing behaviour:** bare-LF line endings
(`GET / HTTP/1.1\nHost: x\n\n`) get no response — the parser requires CRLF, so the request is
held as incomplete until the 10s header deadline closes it. RFC 9112 permits requiring CRLF, so
this is correct, but it is the kind of thing worth being able to explain rather than discover.

---

## Fix report — all 10 closed

| ID | Severity | Files touched | Verified by |
|----|----------|---------------|-------------|
| B1 | High | `src/Server.cpp:350` | half-close response byte-identical to control |
| B2 | High | `includes/Connection.hpp:28`, `src/Connection.cpp:16,67,243` | outage 30.1s → 10.0s; keep-alive 4/4 over 36s |
| B3 | Medium | `includes/RequestParser.hpp:39`, `src/RequestParser.cpp:68,114` | 5 encoded paths now 200; **8/8 traversal still 400** |
| B4 | Medium | `src/RequestParser.cpp:267,305` | 6 terminator forms, 4 previously 400, now 201 |
| B5 | Low | `src/RequestParser.cpp:250` | 6 malformed sizes now 400, 2 valid still 201 |
| B6 | Low | `src/RequestParser.cpp:181` | ceiling exactly 8192; 9000 bytes now 431 |
| B7 | Low (latent) | `src/Connection.cpp:16` | closed with B2 (same edit) |
| B8 | Cosmetic | `includes/Connection.hpp` | rebuilds clean |
| B9 | Cosmetic | `www/private/secret.txt` (new) | `/private/` now 403, was 404 |
| B10 | Housekeeping | branch state | `ce8a96b..a23336a` fast-forward; escaping test runnable |

### Full regression run after all fixes

```
=== B3  percent-decoding ===                       6/6
=== B3  TRAVERSAL REGRESSION (critical) ===        8/8
=== B4  chunked terminator ===                     9/9
=== B5  chunk size strictness ===                  8/8
=== B6  header ceiling ===                         4/4
=== B9  /private 403 ===                           2/2
=== smoke / regressions ===                       14/14

TOTAL: 51/51 passed
```

Plus, separately: the B2 silent-socket test (recovery at 10.0s, 128 header-deadline reaps), the
B2 keep-alive test (4/4 over 36s), fds **9 → 9**, RSS **992 KB → 1.68 MB** across the whole run.

### Two notes for the defence

**B3 changed the nature of the traversal defence.** Before, `%2e%2e%2f` was safe because it was
never decoded — it was looked up as a literal filename and 404'd. Now it decodes to `../` and is
rejected by the segment scan in `Router::validateUrlPath()`. Same result, different reason, and
the second reason is the defensible one. The ordering that makes this true is one line:
`percentDecode()` runs before `validateRequestLine()`, never after.

**B4 was two bugs wearing one coat.** The reported symptom was the literal `"0\r\n\r\n"`
comparison at line 264, but trailers actually died earlier, in `getChunkIndex()`'s
data-trailing-CRLF check. Fixing only the reported line would have left
`0\r\nX-Sum: abc\r\n\r\n` returning 400 — worth remembering as an example of why the fix went
where the failure was, not where the complaint was.

## Test provenance and caveats

Worth recording, because "it passed" is only as good as the build it passed on.

**Which build produced which numbers.** The adversarial run overlapped the cleanup pass. By the
time it finished, `src/CgiProcess.cpp`, `src/ConfigParser.cpp`, `src/Response.cpp` and
`src/ServerCgi.cpp` had been modified, and the binary was rebuilt underneath it (the `<cerrno>`
removal). So the CGI and config results in the second adversarial pass predate the cleanup of
those four files.

What was re-verified afterwards, on the final binary:

| Re-checked after cleanup | Result |
|---|---|
| Full 51-test suite | 51/51 |
| CGI `Status:` override (the `atoi` → `strtol` path) | `Status: 418 I am a teapot` → 418 |
| 404 error page (the removed hardcoded branch) | served from `errors/404.html`, 638 bytes |
| N1 both variants | reproduced on the final binary |
| N2 all three server blocks | live on the final binary |

Not re-run on the final binary: the CGI timeout test, the 2 MB CGI output cap, the 300-sequential
and 200-concurrent resource runs, and `leaks`. The cleanup touched CGI *status-line parsing* only,
not the timeout, the output cap or the fork/wait logic, so these are expected to hold — but
"expected to hold" is not "measured", and they should be re-run before the defence.

**A corrected measurement.** The CGI timeout was first reported at 24.5s, which would have been a
bug. It was wrong: the 24.5s was the test helper draining to its own socket timeout, not server
latency. Time to first byte is 4.55s against a `CGI_TIMEOUT_SECONDS` of 5. Recorded because the
retraction is the useful part — a timeout measured from the wrong side of the socket looks like a
server fault.

**One caveat that resolved itself.** The Host-header decoding tests came with the honest note that
both cases land on the default server anyway, "so it can't demonstrate vhost confusion until
multiple ports are wired up". That premise came from the stale warning in N2. Multiple ports *are*
wired up, so the vhost path is real and `other.local` does resolve to its own root — but the
specific question of whether an encoded Host could be made to select the *wrong* vhost is still
untested, and now testable.

**Mode 2 (the knowledge interrogation for defence prep) is blocked while N1 is open.** When N1 is
fixed, only the two N1 reproductions plus the Content-Length contrast need re-running, not the full
suite.

---

## Cleanup pass — done

Non-bug tidying, applied after the ten fixes and re-verified with the same 51-test suite (51/51).

| # | Item | What was done |
|---|------|---------------|
| 1 | Dead `setServerConfig()` in `src/ServerCgi.cpp` | Removed (32 lines). It hardcoded a config from before the parser existed and had **zero** references. |
| 2 | `std::atoi` at `CgiProcess.cpp:177` | Replaced with an explicit `std::isdigit` scan over all three characters plus `std::strtol`. `atoi` cannot report failure; the old code leaned entirely on the `100..599` range check to catch garbage. |
| 3 | Stale 404 branch in `Response.cpp` | Removed. It emitted a bare Bootstrap-classed fragment — no doctype, no `<html>`, and two dead `href="#"` links — while the generic branch beside it emitted a complete document. Dead in practice anyway: `config/default.conf:16` maps 404 to `./errors/404.html`, so `errorPage()` returned first. |
| 4 | Dead local `status` in `setDefaultErrorBody()` | Removed. Assigned, never read; no warning because `std::string` is non-trivial so `-Wunused-variable` stays quiet. |
| 5 | Dead `#include <cerrno>` in `src/Connection.cpp` | Removed — `errno` appeared only in the comment next to the include. |
| 6 | `setMAxBodySizeLimit` typo | Renamed to `setMaxBodySizeLimit` (2 sites, `src/ConfigParser.cpp`). |
| 7 | `size_t i` shadowing in `Server::addToExistingListener` | Inner loop variable renamed to `j`. It worked only because the inner scope closed before `listeners_[i]` was read again — one line moved and it would have indexed the wrong vector. |
| 8 | Missing includes for the new `strtol` path | `<cctype>` and `<cstdlib>` added explicitly to `src/CgiProcess.cpp` rather than relying on transitive includes. |
| 9 | `graphify-out/` showing as untracked | Added to `.gitignore` — it is regenerated by `graphify update .`. |

Re-verified after the cleanup: 51/51, build clean, CGI `Status:` override still honoured
(a script returning `Status: 418 I am a teapot` yields 418, exercising the new `strtol` path), and
404 still served from `errors/404.html` at 638 bytes.

**Deliberately not done:** `errors/` holds six unreferenced images (`images.jpeg`, `images1.jpeg`,
`images3.jpeg`, `errorPage_NO.jpeg`, `errorPage_No2.jpeg`, `husky-dog-no-no-no.png`). They are
your assets, not generated files, so deleting them is your call, not a cleanup decision.

### Still open (not bugs)

- `README.md` is empty and is mandatory. Largest remaining gap to a passing defence.
- `routeDelete` falls back to `mapRootPath` — DELETE scope needs a decision.
- Browser and NGINX comparison testing not yet done.
- The Makefile link step swallows linker errors and then runs `make clean`, deleting the evidence.
- **CGI output cap**: 2 MB of CGI output returns 500 (`CGI_OUTPUT_TOO_LARGE`). Bounded and safe,
  but large CGI output fails rather than streaming. Worth a deliberate decision on the cap value
  rather than leaving it as an accident.
- N1 and N2 above are open.
