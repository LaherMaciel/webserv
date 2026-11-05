# DELETE scope — what, why, how, where

**Date:** 2026-10-29
**File changed:** `src/Router.cpp`, function `Router::routeDelete()` — 6 lines
**Status:** implemented, 18/18 targeted tests pass, full 51-test suite still 51/51

---

## 1. WHAT was wrong

`DELETE` could remove **any file under a location's `root`** whenever that location had no
`upload_store` directive.

The shipped `config/default.conf` does not expose this, because the only location that allows
`DELETE` is `/upload`, and `/upload` does have an `upload_store`. So the hole was **latent**: it
required a one-word config change to become real — and editing the config is exactly what a peer
evaluator does during a defence.

### Proof, measured rather than argued

A config with `location /` set to `allowed_methods GET DELETE`, `root` pointing at a directory
holding `index.html` and `config.bak`, and **no `upload_store`**:

| Binary | `DELETE /index.html` | `DELETE /config.bak` | Files afterwards |
|---|---|---|---|
| Before the fix (commit `cfca097`) | **200 OK** | **200 OK** | **both destroyed** |
| After the fix | **403 Forbidden** | **403 Forbidden** | both intact, `GET` still 200 |

The "before" column was produced by building commit `cfca097` in a separate git worktree and
running it against the same config and the same two requests, so the comparison is between two real
binaries rather than between code and an assumption.

---

## 2. WHY it happened

`routeDelete()` had a two-branch fallback:

```cpp
    std::string filePath;
    if (!location->uploadStore_.empty())
        filePath = mapUploadPath(location, request.getPath());
    else
        filePath = mapRootPath(request.getPath(), location);
```

The two helpers have **very different safety properties**, and the fallback silently picked the
unsafe one:

| Helper | What it accepts | Confinement |
|---|---|---|
| `mapUploadPath()` (`src/Router.cpp:219`) | one flat filename — throws 400 if the remainder contains `/` | file must sit **directly inside** `upload_store` |
| `mapRootPath()` (`src/Router.cpp:55`) | any suffix, including nested directories | anywhere **under** `root`, at any depth |

`mapRootPath` is the correct helper for `GET`, where reading a nested file is the whole point. It is
the wrong helper for `DELETE`, where "any depth under root" means the entire served site is
writable by anyone who can reach the port.

The underlying mistake is **symmetry that is not actually symmetric**: `GET` and `DELETE` both map a
URL to a filesystem path, so sharing the mapping looks natural. But a read and an unlink do not
deserve the same reach. `GET` exposing a file is recoverable; `DELETE` removing it is not.

### Why `upload_store` is the right gate

`POST` can only ever create files inside `upload_store` (`routeUpload` → `mapUploadPath`). Making
`DELETE` use the same gate gives one sentence that describes the whole policy and that is defensible
out loud:

> **You may delete only what you could have uploaded.**

Any other scope requires answering "why is *this* subtree deletable but not that one?" — and the old
answer was "because whoever wrote the config forgot a directive", which is not an answer.

---

## 3. HOW it was fixed

```cpp
RouteType Router::routeDelete(const Request& request, const LocationConfig *location, Response& response)
{
    std::cout << "Routing DELETE request for path: " << request.getPath() << "\n";
    if (location->uploadStore_.empty())
        throw 403;
    std::string filePath = mapUploadPath(location, request.getPath());
    if (filePath.empty())
        throw 403;
    removeFile(filePath);
    response = Response(200, request.getVersion(), "File deleted\n", "text/plain");
    return ROUTE_DELETE;
}
```

Four deliberate decisions:

**(a) The `mapRootPath` fallback is deleted outright, not narrowed.** A narrowed fallback would still
need a rule for how deep is acceptable, and that rule would be arbitrary. Removing the branch means
there is no second path to reason about.

**(b) `throw 403` on an absent `upload_store`, not 404 or 405.** 405 is wrong: the config *did* allow
`DELETE` on this route, so the method is permitted. 404 is wrong and actively harmful: it would leak
whether the file exists, letting an attacker map the filesystem by probing. 403 states the honest
reason — the request is understood, the method is allowed here, and the server refuses to act.

**(c) The `uploadStore_.empty()` check is kept even though `mapUploadPath()` already throws 403 for
the same condition.** This is intentional redundancy. The check *is* the policy statement, and it
belongs at the point where the policy is decided, not buried in a path-building helper. If
`mapUploadPath` is ever reused or changed, the policy does not silently move with it.

**(d) The success body changed from `"DELETE request received"` to `"File deleted\n"`.** The old text
described the *request*, not the *outcome* — it would have been returned verbatim even if the
semantics had been wrong. `204 No Content` would also be correct and arguably more idiomatic; `200`
with a one-line body was kept because it makes the result visible in `curl` without `-i`, which
matters during a live defence.

### What was already correct and was left alone

`removeFile()` (`src/Router.cpp:256`) already maps `errno` properly, so the fix did not need to
touch error handling:

| `errno` | Status | Meaning |
|---|---|---|
| `EACCES`, `EPERM`, `EROFS`, `EISDIR` | 403 | permission denied, or the target is a directory |
| `ENOENT`, `ENOTDIR` | 404 | no such file |
| anything else | 500 | genuine server fault |

Traversal was already closed upstream: `Router::validateUrlPath()` rejects `.` and `..` segments
before routing, and percent-decoding runs **before** that check, so `%2e%2e%2f` becomes a real `../`
and is caught rather than slipping through as a literal filename.

---

## 4. WHERE it lives

| Concern | Location |
|---|---|
| The policy decision | `src/Router.cpp:270-271` — `if (location->uploadStore_.empty()) throw 403;` |
| Path construction, confined to one flat filename | `src/Router.cpp:219` — `Router::mapUploadPath()` |
| `errno` → HTTP status | `src/Router.cpp:256` — `Router::removeFile()` |
| Method permitted per route | `src/Router.cpp` — `Router::isValidMethod()`, from `allowed_methods` |
| `..` / `.` segment rejection | `src/Router.cpp:44` — `Router::validateUrlPath()` |
| Percent-decoding (must run before the above) | `src/RequestParser.cpp:68` — `RequestParser::percentDecode()` |
| The directive this now depends on | `src/ConfigParser.cpp:168` — `upload_store` |
| Config that exercises it | `config/default.conf:38-45` — `location /upload` |

---

## 5. Test results

18/18. The `403` rows are the fix; everything else is proof the fix did not overreach.

### Inside the scope (`upload_store` configured)

```
DELETE existing file in upload_store          200 OK      file actually gone
DELETE same file again                        404          correct, not 500
DELETE a directory inside upload_store        403          directory survived
DELETE nested path   /upload/a/b.txt          400          mapUploadPath rejects '/'
DELETE bare location /upload                  400          no filename given
DELETE /upload/../index.html                  400          www/index.html survived
DELETE /upload/%2e%2e%2findex.html            400          encoded form also blocked
```

### Round trip

```
POST   /upload/round.bin   201 Created    file present
DELETE /upload/round.bin   200 OK         file gone
```

### Outside the scope

```
DELETE /index.html          (root, no upload_store)   403    www/index.html survived
DELETE /static/.../alpha    (DELETE not allowed)      405    alpha.txt survived
```

Note the two different refusals, which is the point: **405** means "not allowed on this route",
**403** means "allowed here, but this server will not delete outside `upload_store`".

Full suite after the change: **51/51**.

---

## 6. Trade-off to be aware of

`DELETE` is now confined to a **single flat directory** per location. Deleting
`/upload/2026/report.pdf` is impossible even if that file was uploaded, because `mapUploadPath`
rejects any remainder containing `/`.

That is a restriction inherited from `POST`, which cannot create nested paths either, so the two
remain consistent — you can still delete everything you can create. If nested uploads are ever
added, **both** helpers must change together, and the depth limit must be explicit rather than a
side effect of a `find('/')` check.

A second, smaller inconsistency worth knowing: `GET` resolves through `root_` while `POST`/`DELETE`
resolve through `uploadStore_`. In `config/default.conf` they are the same directory
(`./www/upload`), so uploads are immediately readable. If a config sets them differently, a file
could be uploaded and deleted but never served. Not a bug — a config footgun worth documenting.
