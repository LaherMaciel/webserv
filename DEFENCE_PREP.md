# Defence prep — five questions on the code we changed under pressure

**Date:** 2026-10-29

These five came out of an adversarial Mode 2 pass. They target the places where the *reasoning* is
less obvious than the code — which is exactly where a peer evaluator pushes, because that is where
the difference between "wrote it" and "understands it" shows.

**Use this as a self-test.** Answer all five out loud, in your own words, before reading Part 2.
Saying them out loud matters: the defence is spoken, and an answer that feels solid in your head
often falls apart when you have to name the function that does the work.

---

## Part 1 — The questions

### Q1. Percent-decode ordering

`percentDecode()` runs at `src/RequestParser.cpp:114`, **before** `validateRequestLine()` and
therefore before `Router::validateUrlPath()`.

> Walk me through the exact bytes an attacker sends to try to read `/etc/passwd`, and show me the
> precise point in your code where it dies.
>
> Now move `percentDecode()` to **after** `validateUrlPath()` and trace the same bytes again. Where
> does it die — or does it?

### Q2. Why one decode pass

`%252e%252e%252f` returns **404**, not 400, and that is correct.

> Why 404? What would a second decode pass do, and why don't you do one?

### Q3. Where the N1 cap had to live

The buffer cap is at `src/Connection.cpp:258`, not in `RequestParser` next to the other body-size
checks.

> Why can't this check live in the parser? What does `parseRequestBody()` return when I send a
> chunk-size line with no CRLF, and why does that defeat a parser-side cap?

### Q4. Why `CONN_NEW` is a separate state

> Why is `CONN_NEW` a separate state? Why not just add `CONN_WAITING_FOR_REQUEST` to the header
> deadline check and delete `CONN_NEW`? Show me the one client that breaks if you do that.

### Q5. What `client_max_body_size` actually is

> Finish this sentence and defend it: "`client_max_body_size` is not really a body-size limit, it is
> a ______."
>
> Given `MAX_CONNECTIONS` is 128, what is the server's worst-case memory from body buffers, and
> which config line is the only knob that changes it?

---

## Part 2 — What a strong answer contains

Read only after you have answered all five.

### A1

Decoding `%2e%2e%2f` turns it into `../` **upstream** of the segment scan in
`Router::validateUrlPath()` (`src/Router.cpp:44`), which walks the path segment by segment and
rejects any segment equal to `.` or `..` with 400.

If decoding ran *after* validation, `validateUrlPath()` would see the still-encoded
`%2e%2e%2f`, find no literal `..` segment, pass it as a perfectly ordinary filename — and *then*
decoding would produce `../`, which goes straight to the filesystem. **The ordering is the entire
defence.** The check itself is unchanged in both cases; only its position makes it work.

The trap to avoid: `validateRequestTarget()` also inspects `%` sequences, but it only verifies that
each `%` is followed by two hex digits. It is a syntax check, not a traversal defence. Naming it as
the thing that stops traversal is the red-flag answer.

### A2

`%25` is the encoding of a literal `%`. One pass turns `%252e` into `%2e` — four characters of
ordinary text, not a dot. So the server looks for a file whose name literally contains `%2e`, does
not find one, and returns **404**. The request was well-formed; there is nothing to reject, so 400
would be wrong.

A second pass would turn that `%2e` into `.`, reconstructing `../` **after** validation had already
run. That is the classic double-decode traversal bug. One pass is a deliberate choice: the raw bytes
are validated, decoded exactly once, and never re-fed to the decoder.

### A3

When the chunk-size line or the trailer section is incomplete, `getChunkIndex()` and
`copyByChunks()` return **-1** before the `bodySize + n > maxBodySize_` check can execute — that
check needs a fully parsed size line to have an `n` at all. So a parser-side cap on body bytes
**never runs on this path**. This is a reachability problem, not a logic problem: the check would be
correct and simply never execute.

Meanwhile the unparsed bytes are sitting in `Connection::in_buffer_`, which keeps growing because
`readFromSocket()` appends every `recv`. So the ceiling has to be enforced where the bytes
accumulate — in `Connection`, **after** `in_buffer_.erase(0, parser_.endOfBody_)` so it counts only
genuinely unparsed bytes, and **inside the existing `try`** so `throw 413` becomes a response rather
than escaping to `main`.

Worth saying out loud: this is the same mistake that was made once already, during the original
chunked 413 work, where a correct check was placed after an early `return` and never ran.

### A4

`resetForNewRequest()` (`src/Connection.cpp:50`) puts an **established keep-alive** connection back
into `CONN_WAITING_FOR_REQUEST` between requests. If the 10-second header deadline covered that
state, a legitimate client idling 11 seconds between two requests on one keep-alive connection would
be killed — but that client should get the 30-second idle timeout, not the header deadline.

`CONN_NEW` distinguishes "brand new, has never sent a byte" (10s) from "sitting between requests on
an established connection" (30s). Verified live: requests at t=0 and t=15s both return 200 on the
same socket, while a silent brand-new socket is reaped at 10.0s.

The answer "to make the code clearer" is the red flag. It is not a readability change; the one-line
alternative is a functional regression.

### A5

It is a **per-connection memory budget**.

Worst case ≈ `MAX_CONNECTIONS × (largest client_max_body_size across all locations + MAX_HEADER_SIZE)`
plus allocator overhead. The only knob is the **largest** `client_max_body_size` in the config,
because the pre-filter uses the maximum reachable limit — routing has not happened yet when the
parser needs a threshold.

This is why a config with `client_max_body_size 100000000` authorises a ~100 MB connection: a single
client offering 12 MB of garbage framing gets no 413 at all, because 12 MB is under the limit it was
told to accept, and RSS hits ~754 MB for that one connection. That is the config's policy working as
written, not a bug. Measured: at the shipped 5 MB limit, 128 concurrent attackers peak at ~822 MB and
the connection cap starts answering 503, which is the real ceiling.

The red flag is "it limits how big a POST can be" and stopping there — true but incomplete, and it
misses that raising the number raises worst-case server memory.

---

## One extra thing to be ready for

**Settled RSS does not return to baseline after a memory attack.** 100–445 MB has been observed with
zero attackers still connected. A grader watching `top` may notice and ask.

The answer: freed, not leaked — the process allocator retains the pages rather than returning them
to the OS. The evidence is `leaks` reporting **0 leaks / 0 bytes** and file descriptors back to 8.
Fragmentation and retention, not a leak.

---

## How to use the result

If any of the five felt shaky, that is the gap to close before the defence — not by rereading the
answer here, but by going back to the code and re-deriving it. The questions were built so that the
red-flag answers are the ones that sound fine until someone asks the follow-up.
