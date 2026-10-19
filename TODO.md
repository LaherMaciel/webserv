# webserv — work plan

Agreed order, 2026-10-17. Rationale: the bugs below are what make the server
*look* broken to an evaluator. Features earn marks on top of a grade you still
have; a server that answers nothing to `nc -N` and dies at ten connections
fails the defence regardless of how many directives it honours.

Findings reference `STRESS_TEST_REPORT.md`.

## 1. Bugs that cost the grade — do these first

- [ ] **Finding 5 — `config/default.conf:41` upload path.**
      Config says `upload_store ./www/uploads`; the directory on disk is
      `www/upload`. `POST /upload/t.txt` returns 500 against the project's own
      default config. One-word fix. Highest visibility for the least work.
- [ ] **Finding 2 — half-closed connections, `src/Server.cpp:207-211`.**
      The `POLLERR|POLLHUP|POLLNVAL` branch runs before `POLLIN` and
      `continue`s, so a FIN arriving in the same poll cycle as the data
      discards a complete request. `printf ... | nc -N` gets no response at
      all. Reorder so pending input is read before a hangup is honoured.
- [ ] **Finding 3 — `MAX_CONNECTIONS 10`, `includes/webserv.hpp:5`.**
      Ten idle sockets are a total denial of service. The subject says
      "remain available under stress testing" and "resilience is a central
      evaluation concern". Raise the cap and return 503 instead of a silent
      RST when full.
- [ ] **Finding 6 — DELETE scope, `src/Router.cpp:291-294`.**
      With no `upload_store`, DELETE falls back to `mapRootPath` and removes
      any file under `root`. An evaluator can delete `index.html` mid-demo.
      Needs a decision: confine DELETE to `upload_store`, or refuse it when
      `upload_store` is unset.

## 2. Mandatory config features — cheapest first

- [ ] **`returnCode_` / `returnTarget_` (redirects).** Smallest of the three:
      return the status and a `Location:` header before any file lookup.
      Currently `return 301 /` gives 404.
- [ ] **`errorPages_` (custom error pages).** A lookup in the error path with a
      fallback to the built-in page. Currently the built-in page is always used.
- [ ] **`autoindex_` (directory listing).** Biggest of the three: needs
      `opendir`/`readdir` and HTML generation. Currently a directory with no
      index returns 403 even with `autoindex on` — note 403 is the correct
      answer for autoindex *off*, so only the listing branch is missing.

## 3. Large items

- [ ] **Client idle timeout.** Not in the 12 findings because it was already
      known, but it is an absolute in the subject ("requests must never hang
      indefinitely") and it compounds findings 3 and 8 — a bare-LF request
      holds an fd forever, and with a cap of 10 that is a trivial DoS. Track
      `last_activity` per connection and reap stale ones. ~1 hour, closes
      three things at once.
- [ ] **Multiple ports.** Mandatory, 2-4h, touches the poll loop. The loop goes
      around *binding*, not serving: `runServer()` is an infinite poll loop, so
      one `Server` must own several listening sockets in one `poll_fds_`.
      `config_`, `fd_`, `port_` and `router_` all become plural, each
      `Connection` must remember which listener accepted it, and configs must
      be grouped by `host:port` because `default.conf` has 3 server blocks but
      only 2 unique pairs — `bind()` fails on the second 8080. First block for
      a pair is the default; choosing between them needs the Host header,
      which is out of scope.
- [ ] **Finding 1 — body copied on every `recv()`.** 16 MB upload reaches
      1.03 GB RSS, 49 MB reaches 3.0 GB, time is quadratic. The only bug that
      lets a client kill the server outright, so it cannot be skipped, but it
      is also the most likely to break working code. Parse against the buffer
      by index instead of assigning `rawRequestLine_`
      (`src/RequestParser.cpp:276`) and appending after re-reading
      `getBody()` (`src/RequestParser.cpp:182`). Do it with a clear head and
      the test suite to hand.
- [ ] **Finding 4 — response splitting on CGI POST.**
      `src/ServerCgi.cpp:98-132` can take both the EOF-success branch (200)
      and the write-error branch (500) for one request, and
      `src/Connection.cpp:169` appends without checking for an already-queued
      response. Fix the two completion paths, not just the symptom.

## 4. Status-code corrections — small and independent

- [ ] Finding 7: `ENAMETOOLONG` → 404 not 500 (`src/Router.cpp:46-56`)
- [ ] Finding 8: bare-LF request never answered, fd held forever
      (`src/RequestParser.cpp:144`)
- [ ] Finding 9: the 8192 header cap does not apply to complete requests
      (`src/RequestParser.cpp:147`)
- [ ] Finding 10: unimplemented methods → 501 not 400
      (`src/RequestParser.cpp:20`)
- [ ] Finding 11: legal chunked trailers rejected (`src/RequestParser.cpp:259`)
- [ ] Finding 12: upload errors → 403/409/414 not 500
      (`src/Router.cpp:229-235`)

## 5. Last

- [ ] **README.md.** Currently 0 lines and mandatory. Needs the italicised
      first line declaring the 42 curriculum and the logins, plus
      `Description`, `Instructions` and `Resources` sections, the last
      explaining how AI was used and on which parts.

## Also outstanding

- [ ] `tests/body_tests.sh` section D is stale — it expects Transfer-Encoding
      to win over Content-Length, which is now correctly a 400. 2 false
      failures until rewritten.
- [ ] Dead code: `setServerConfig()` at `src/ServerCgi.cpp:7`. Unreachable
      (no declaration, no caller) since `main.cpp` falls back to
      `DEFAULT_CONFIG`. Beth re-added it in her branch, so agree before
      deleting.
- [ ] Off-list functions that will be questioned: `std::atoi` at
      `src/CgiProcess.cpp:177`, `unlink` at `src/Router.cpp:277`. The second
      is unavoidable — DELETE is mandatory and nothing on the authorised list
      removes a file.
- [ ] `server_name` is stored as one string, not a list, so it will never match
      a Host header once virtual hosting exists. Out of scope per the subject.
