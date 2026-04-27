# Git Server Spec

A small, single-tenant git HTTP server that runs on the X4, receives `git push` from the developer's machine, and atomically deploys updated plugins, translations, fonts, and other SD-card content to the device.

The motivation is workflow parity: the developer wants to use their normal dev tools (editor, git CLI, branches, history) and have the X4 receive the result of `git push x4 main` rather than manually copying files via WebDAV or SFTP.

This spec covers the runtime-side server only. The developer's machine uses stock git — no custom client, no proprietary protocol.

---

## Goals

1. **Stock git client compatibility.** `git push https://x4.local/repo.git main` works from any unmodified git installation.
2. **Atomic deploys.** A push either succeeds entirely or fails entirely. No partial states.
3. **Automatic checkout into runtime directories.** Successful pushes update `/plugins/`, `/languages/`, `/fonts/`, etc. without manual intervention.
4. **Live plugin reload.** After a successful push, the plugin manager picks up changes within seconds — no device restart required.
5. **Version history on the device.** Every push is a commit; previous commits remain on the SD card and can be redeployed by pushing them.
6. **Branch-based feature gating.** Different branches can deploy to different runtime modes (e.g. `experimental` branch vs `main`).
7. **Minimal C surface.** Most of the implementation lives as Lua plugins on the SD card, updatable without reflashing.
8. **Reasonable memory and time budget on the C3.** Peak RAM during a push under 80 KB; small change pushes complete in under 30 seconds.

## Non-goals

- **Hosting public repos.** This is single-developer, single-device. No multi-user accounts, no permission models, no organization features.
- **A web UI.** Push and pull happen via the standard git CLI, not a browser.
- **Garbage collection, repacking, or `git fsck` on device.** The server accepts pushes and stores objects loosely. Cleanup is offline, run on the SD card from a dev machine when needed.
- **Full git porcelain.** No `git merge`, `git rebase`, `git cherry-pick` semantics on device. The X4 is a receiver, not a development environment.
- **Multiple repos.** One repo per device in v1. Could be revisited later, but adds path-routing complexity not worth the v1 cost.
- **SSH transport.** HTTP only. SSH would require a separate protocol stack and crypto surface that aren't needed for this use case.

---

## User flow

### One-time setup

1. User opens **Settings → Git Server** on the device.
2. User toggles "Enable Git Server" on.
3. The device:
   - Generates a 32-byte random access token, stores it in NVS.
   - Initializes a bare repo at `/.git/` on the SD card if one doesn't exist.
   - Starts the git server plugin.
   - Displays the connection info on the e-ink screen:
     ```
     Git Server: https://x4.local/repo.git
     Token: a1b2c3d4e5f6...
     QR: [code]
     ```
4. On their dev machine, the user runs:
   ```sh
   git remote add x4 https://token:a1b2c3d4e5f6...@x4.local/repo.git
   ```

### Daily push

```sh
$ vim community_plugins/notes/notes.lua
$ git add . && git commit -m "fix notes plugin scrolling"
$ git push x4 main
Counting objects: 3, done.
Compressing objects: 100% (3/3), done.
Writing objects: 100% (3/3), 412 bytes | 412 KiB/s, done.
remote: Receiving pack (3 objects)
remote: Validating
remote: Updating refs/heads/main
remote: Running post-receive hook
remote: Checked out 1 file to /plugins/notes/notes.lua
remote: Reloading plugins
remote: Done in 4.2s
To https://x4.local/repo.git
   9a3f1c2..5b8e7d0  main -> main
```

The device's e-ink screen briefly shows a "Receiving push…" indicator and updates after completion.

### Rollback

```sh
$ git push x4 HEAD~1:main --force
```

The device redeploys the previous commit. Same atomicity, same auto-checkout, same plugin reload.

---

## Architecture

The git server is a Lua plugin (`git_server`) that registers HTTP routes on the existing `CrossPointWebServer`-style HTTP server. It depends on three new C-side modules and reuses the existing storage, wifi, and plugin-manager APIs.

```
Dev Machine                              X4 (CrossLua Reader)
┌───────────────┐                        ┌──────────────────────────────────┐
│  git client   │                        │  HTTP server (existing)          │
│               │  HTTPS POST            │  ├─ /git/info/refs        ──┐    │
│  git push     │ ──────────────────────▶│  ├─ /git/git-receive-pack ──┼──┐ │
│  (smart HTTP) │                        │  └─ /git/git-upload-pack  ──┼──┤ │
└───────────────┘                        │                              │  │ │
                                         │  Lua plugins:                │  │ │
                                         │  ├─ git_server.lua        ◀──┘  │ │
                                         │  ├─ git_pack.lua          ◀────┘ │
                                         │  ├─ git_objects.lua              │
                                         │  ├─ git_refs.lua                 │
                                         │  └─ git_hooks.lua                │
                                         │                                  │
                                         │  C runtime additions:            │
                                         │  ├─ crypto.sha1                  │
                                         │  ├─ zlib.inflate / .inflate_*    │
                                         │  └─ http.on_post_stream          │
                                         │                                  │
                                         │  SD card:                        │
                                         │  ├─ /.git/                       │
                                         │  ├─ /plugins/  (auto-checkout)   │
                                         │  ├─ /languages/                  │
                                         │  └─ /fonts/                      │
                                         └──────────────────────────────────┘
```

---

## Transport

Git smart HTTP, exactly as specified in the [git documentation](https://git-scm.com/docs/http-protocol). Two URLs:

| Method | Path                             | Purpose                          |
|--------|----------------------------------|----------------------------------|
| GET    | `/git/info/refs?service=<svc>`   | Ref discovery (push and pull)    |
| POST   | `/git/git-receive-pack`          | Receive a push from the client   |
| POST   | `/git/git-upload-pack`           | Serve a fetch/clone to a client  |

`<svc>` is `git-receive-pack` for push and `git-upload-pack` for fetch/clone.

The existing reverse-proxy/HTTPS infrastructure handles TLS termination. The git server itself only sees decrypted bytes.

---

## Required new C runtime APIs

The git server plugin should be implementable in pure Lua **plus** the following minimal additions to the C runtime. Each addition is independently useful for other features (signed plugins, TLS-from-Lua, large file uploads).

### `crypto.sha1(data) → hash`

Compute SHA-1 of a byte string. Returns 20 raw bytes. Backed by the C3 hardware accelerator via mbedTLS.

```lua
local sha = require("crypto")
local hash = sha.sha1("blob 4\0test")  -- 20 bytes
```

A streaming variant is optional but useful for hashing objects that don't fit in RAM:

```lua
local h = crypto.sha1_init()
crypto.sha1_update(h, chunk1)
crypto.sha1_update(h, chunk2)
local hash = crypto.sha1_final(h)
```

### `zlib.inflate(data) → data, errmsg`

Decompress a complete zlib stream. Returns the decompressed bytes or `nil, errmsg`.

For large objects, a streaming variant is required so that we don't have to buffer entire compressed blobs in RAM:

```lua
local stream = zlib.inflate_init()
local out, err = zlib.inflate_chunk(stream, in_chunk)
local final, err = zlib.inflate_finish(stream)
```

The streaming variant is the load-bearing primitive for pack-file processing.

### `http.on_post_stream(path, on_chunk, on_complete) → handler_id`

Register a streaming POST handler. Instead of buffering the entire request body in RAM (which the existing `WebServer.h` pattern does), the server invokes `on_chunk(data)` for each chunk as it arrives, then `on_complete(success, headers)` when the body finishes.

```lua
http.on_post_stream("/git/git-receive-pack", function(chunk)
    -- write chunk straight to SD
    pack_writer:write(chunk)
end, function(success, headers)
    -- post-process the received pack
    process_pack()
end)
```

This is the load-bearing primitive that lets us receive multi-MB packfiles without exhausting RAM.

### Reused (already exists)

- `storage.*` — file I/O on SD card
- `wifi.*` — TLS, network primitives
- `system.log` / `LOG_*` — logging
- The HTTP route registration mechanism the project already uses

---

## Lua module breakdown

All in `sdcard/plugins/git_server/` as a single plugin folder:

```
git_server/
├── init.lua            -- Plugin entry, registers HTTP routes
├── lib/
│   ├── pack.lua        -- Packfile parser
│   ├── objects.lua     -- Bare object database on SD
│   ├── refs.lua        -- Atomic ref management
│   ├── pkt_line.lua    -- Smart-HTTP pkt-line framing
│   ├── auth.lua        -- HTTP Basic + token auth
│   └── hooks.lua       -- Post-receive hook runner
├── default_post_receive.lua   -- Default auto-checkout hook
└── README.md
```

### `init.lua`

- Reads enabled/disabled state from settings.
- Registers `/git/info/refs`, `/git/git-receive-pack`, `/git/git-upload-pack` routes.
- Initializes `/.git/` directory layout on first run.

### `pack.lua`

- Parses pack file headers and object headers.
- Decompresses object bodies via streaming `zlib.inflate`.
- Resolves `REF_DELTA` and `OFS_DELTA` deltas using the SD-stored object database.
- Emits one fully-resolved object at a time to `objects.lua` for storage.

### `objects.lua`

- Bare object DB at `/.git/objects/`.
- Loose objects only — `objects/aa/bbcc...` layout where `aabbcc...` is the SHA-1.
- Stores objects as zlib-compressed blobs (matching git's on-disk format).
- Lookup by SHA, write by SHA.

### `refs.lua`

- Read/write refs at `/.git/refs/heads/<branch>`.
- Atomic updates: write to `<ref>.lock`, fsync, rename.
- HEAD is `/.git/HEAD`, a symref to a branch.

### `pkt_line.lua`

- Encode/decode smart-HTTP pkt-line framing (4 hex bytes of length + payload).
- Used for the discovery and update flows.

### `auth.lua`

- Parse `Authorization: Basic <base64>` header.
- Compare token against the one stored in NVS.
- Constant-time comparison.

### `hooks.lua`

- Loads and runs `/.git/hooks/post-receive.lua` if present.
- Falls back to `default_post_receive.lua` shipped with the plugin.
- Hook is sandboxed Lua: no `os.execute`, no shell access, no escape from the runtime sandbox.

### `default_post_receive.lua`

- Reads the new HEAD commit's tree.
- For each tracked file, copies it to its corresponding runtime location on SD:
  - `community_plugins/<name>/...` → `/plugins/<name>/...`
  - `sdcard/plugins/<name>/...` → `/plugins/<name>/...`
  - `sdcard/languages/<code>/...` → `/languages/<code>/...`
  - `sdcard/fonts/<name>` → `/fonts/<name>`
- Signals the plugin manager to reload after copying.

The user can override this with their own `post-receive.lua` for custom deploy logic.

---

## SD card layout

```
/                           SD root
├── .git/                   Bare repo
│   ├── HEAD                "ref: refs/heads/main"
│   ├── config              Repo config
│   ├── refs/
│   │   └── heads/
│   │       └── main        SHA-1 of latest commit on main
│   ├── objects/
│   │   ├── aa/
│   │   │   └── bbcc...     Loose object (zlib-compressed)
│   │   └── ...
│   ├── hooks/
│   │   └── post-receive.lua    User override (optional)
│   └── tmp/
│       └── incoming.pack   Streamed pack during a push (deleted after)
├── plugins/                Runtime plugins (auto-checkout target)
├── languages/              Runtime language packs (auto-checkout target)
├── fonts/                  Runtime fonts (auto-checkout target)
└── books/                  User content (NOT touched by the git server)
```

Note: `/books/` is intentionally outside the git workflow. User books are personal data and shouldn't be pushed/pulled.

---

## Push flow (step by step)

### 1. Ref discovery

`GET /git/info/refs?service=git-receive-pack`

Server response (pkt-line):
```
001f# service=git-receive-pack
0000
00475b8e7d0... refs/heads/main\0report-status delete-refs side-band-64k
0000
```

Server reads `/.git/refs/heads/*` and lists each ref with the current SHA. Capabilities advertised: `report-status`, `delete-refs`, `side-band-64k`. Skipped: `quiet`, `atomic` (single-ref pushes are atomic by default), `push-options` (out of scope for v1).

### 2. Push request

`POST /git/git-receive-pack` with body containing:
- pkt-line ref updates: `<old-sha> <new-sha> <ref-name>\0<capabilities>`
- The packfile (PACK header + objects)

The server:

1. Validates auth (HTTP Basic + stored token).
2. Streams the body via `http.on_post_stream`:
   - Reads ref-update pkt-lines until the `0000` flush packet.
   - Buffers ref updates in RAM (small, ~few hundred bytes).
   - Pipes the rest of the body — the packfile — directly to `/.git/tmp/incoming.pack` on SD.
3. Once the body completes, processes the pack from disk:
   - Validates PACK header (`PACK\0\0\0\2` + object count).
   - Iterates objects: read header, stream-decompress body, compute SHA-1, write loose object to `/.git/objects/aa/bbcc...`.
   - Resolves deltas: for each `REF_DELTA` or `OFS_DELTA`, read base object from disk, apply delta instructions, store resolved object.
4. Validates that all referenced commits resolve to existing objects.
5. Updates refs atomically:
   - For each `<old-sha> <new-sha> <ref>` update:
     - Verifies that the current ref actually equals `<old-sha>` (rejects on race).
     - Writes new SHA to `<ref>.lock`, fsyncs, renames to `<ref>`.
6. Runs the post-receive hook (see [Post-receive hook contract](#post-receive-hook-contract)).
7. Sends `report-status` response over side-band-64k:
   ```
   000eunpack ok
   0019ok refs/heads/main
   0000
   ```
8. Deletes `/.git/tmp/incoming.pack`.

### 3. Failure handling

If any step fails:

- Refs are NOT updated.
- New objects ARE left in `/.git/objects/` (harmless; they become unreferenced).
- `report-status` returns the failure: `ng refs/heads/main <reason>`.
- The dev's client sees a normal git error and the push is rejected end-to-end.
- `/.git/tmp/incoming.pack` is deleted.

This gives the atomicity guarantee: either the new HEAD is on disk and runtime files updated, or nothing visible to the device changed.

---

## Pull flow (step by step)

`POST /git/git-upload-pack` is the symmetric counterpart. Useful for backing up edits made on-device or for fetching from the X4 to a second machine.

The server:

1. Validates auth.
2. Reads the client's `want`/`have` lines.
3. Constructs a packfile of objects the client doesn't have, streamed over side-band-64k.
4. Pack construction is simpler than reception: walk the commit graph from `want`, exclude objects reachable from `have`, send the rest as loose-object pack.

In v1, pack construction can be naive — no thin packs, no delta compression. This makes the resulting pack larger but the code dramatically simpler. Optimization can wait.

---

## Authentication

### v1: shared token via HTTP Basic

- Token is generated on first server enable, stored in NVS.
- Sent as `Authorization: Basic <base64(token:token)>` from the client.
  - The git URL `https://token:<value>@x4.local/repo.git` produces this header automatically.
- Constant-time comparison server-side.
- Auth failure returns 401 with `WWW-Authenticate: Basic realm="x4-git"`.

### Token rotation

- Settings → Git Server → "Rotate Token" generates a new one, invalidates the old.
- The user must update their `git remote` URL on each rotated machine.

### Rate limiting

- After 5 consecutive auth failures, lock out new requests for 60 seconds.
- After 30 failures in 10 minutes, log to e-ink display: "Auth lockout — possible attack."
- Prevents online password guessing on weak tokens (though 32-byte random tokens are uncrackable in practice).

### TLS

- All git endpoints require HTTPS. Plain HTTP returns 426 Upgrade Required.
- TLS uses the existing wifi/HTTPS infrastructure. Self-signed cert is acceptable; users add `-c http.sslVerify=false` or accept the cert on first connection.

### Out of scope for v1

- Multi-user accounts, per-branch permissions, signed pushes (GPG/SSH-signed commits). All considered for later phases if they become useful.

---

## Post-receive hook contract

After a successful push, the server runs the hook. The hook receives the list of ref updates and the new state.

### Hook signature

```lua
-- Loaded from /.git/hooks/post-receive.lua, falls back to default
local function post_receive(updates, repo)
    -- updates: array of { ref, old_sha, new_sha, type ("create"|"update"|"delete") }
    -- repo: { read_object(sha), read_tree(sha), read_blob(sha), get_ref(name) }
    --
    -- Return: { ok=true, message="..." } or { ok=false, message="..." }
end
return post_receive
```

### Default behavior

Iterate over updates. For each update where `ref == "refs/heads/main"`:

1. Walk the new commit's tree.
2. For each blob whose path matches a runtime-target prefix:
   - `community_plugins/<plugin>/<file>` → `/plugins/<plugin>/<file>`
   - `sdcard/plugins/<plugin>/<file>` → `/plugins/<plugin>/<file>`
   - `sdcard/languages/<code>/<file>` → `/languages/<code>/<file>`
   - `sdcard/fonts/<file>` → `/fonts/<file>`
3. Diff against the previous tree; only write changed/new files. Delete files that were tracked but are now removed.
4. After all writes, signal the plugin manager: `system.send_event("plugins_updated")`.
5. Return `{ ok=true, message="Checked out N files, reloaded plugins" }`.

### Hook sandboxing

The hook runs in the standard Lua sandbox (same restrictions as any plugin):
- No `os.execute`, no `io.popen`, no shell access.
- No file access outside the SD card.
- Hook errors don't fail the push (the push already succeeded); errors are logged and reported in the response.

### Custom hooks

Users can replace the default by committing their own `hooks/post-receive.lua` to the repo. The git server uses the on-SD copy at `/.git/hooks/post-receive.lua`, which gets updated automatically when a push includes a new version of it.

This gives the user full programmatic control of what happens on deploy, without ever leaving the Lua sandbox.

---

## Auto-checkout details

The default hook's path-mapping conventions:

| Repo path                          | Runtime path              | Why |
|------------------------------------|---------------------------|-----|
| `community_plugins/<name>/...`     | `/plugins/<name>/...`     | The community catalog mirrors active plugins. |
| `sdcard/plugins/<name>/...`        | `/plugins/<name>/...`     | Direct sdcard mirroring for core/dev plugins. |
| `sdcard/languages/<code>/...`      | `/languages/<code>/...`   | Translation packs deployed live. |
| `sdcard/fonts/<file>`              | `/fonts/<file>`           | Fonts deployed live. |
| Anything else                      | (ignored by default)      | Source files, READMEs, config don't go onto runtime. |

If a user wants a different mapping, they override the hook.

### Plugin reload after checkout

The hook signals the plugin manager via a simple event. The plugin manager:

1. Finishes the currently-rendering frame.
2. Suspends the active plugin (calls `onExit()`).
3. Re-discovers plugins (picks up new ones, drops removed ones).
4. Re-loads the active plugin if its file changed (calls `onEnter()` fresh).
5. Returns to the loop.

This happens within ~1 second of the post-receive hook completing. From the user's perspective, they push and the device reflects the change almost immediately.

---

## Memory budget

| Phase                              | Peak RAM | Notes |
|------------------------------------|----------|-------|
| Idle (server enabled, no traffic)  | ~5 KB    | Route registration, config struct |
| Ref discovery (`info/refs`)        | ~10 KB   | Read refs from SD, format pkt-line response |
| Receiving pack body                | ~10 KB   | Streaming to SD, no buffering |
| Pack processing (per-object)       | ~50 KB   | One object decompressed in RAM at a time, ~32 KB inflate buffer + crypto contexts |
| Delta resolution                   | ~50 KB   | Worst case: base object + delta + result, all in RAM. Larger objects spill to SD-backed temp. |
| Post-receive hook (default)        | ~30 KB   | Tree walk, file copies one at a time |
| Total during peak                  | ~80 KB   | Within budget on the C3 |

Stays well under the 380 KB total RAM ceiling, even with the rest of the runtime active.

---

## Performance expectations

| Scenario | Time |
|----------|------|
| Ref discovery (GET info/refs)             | <100 ms |
| Auth check                                | <10 ms  |
| Push of small change (1-3 files, <50 KB)  | 5-30 s  |
| Push of medium change (10 files, ~500 KB) | 30-90 s |
| First push of full repo (~5 MB)           | 3-5 min |
| Pull of small change                      | 5-15 s  |
| Pull of full repo (~5 MB)                 | 2-4 min |

Bottlenecks (in order):

1. **SD card write speed** — sustaining 1-2 MB/s write is realistic on a Class-10 card.
2. **SHA-1 computation** — even with hardware accelerator, ~50 ms per MB. For a 5MB pack this adds ~250ms total, negligible.
3. **zlib inflate** — software-based; 1-2 MB/s decompression typical on the C3.
4. **WiFi throughput** — typically 1-5 Mbps in practice, often the bottleneck for first-time clones.

These numbers are deliberately conservative. Real-world should beat them for typical small daily pushes.

---

## Security considerations

### Threat model

- Adversary is on the same WiFi network OR the device is exposed to the open internet.
- Goals: (a) prevent unauthorized writes, (b) prevent unauthorized reads (private books, notes), (c) prevent denial-of-service via auth pounding.

### Mitigations

- **HTTPS only.** No plain HTTP for git endpoints.
- **32-byte random token.** Brute-force infeasible. Constant-time comparison.
- **Rate limiting.** 5 fails → 60s lockout. 30 fails / 10 min → audible/visible alert on device.
- **Bound resource consumption.** Reject pushes with packs >50 MB (configurable). Reject single objects >5 MB. Reject ref-update lists >100 refs.
- **Sandboxed hooks.** Hooks run in the same Lua sandbox as plugins — no shell, no FS escape, no syscalls.
- **No git GC ever runs server-side.** Eliminates an entire class of "malicious objects exploit gc bug" issues.

### Known limitations

- A user with the token has full write access to `/.git/` and runtime directories. There is no per-branch or per-path access control. This is fine for single-developer use.
- No protection against a stolen device — anyone with physical access to the SD card has everything.
- No detection of malicious commits via signing. A stolen token + a malicious push could deploy bad plugins. Mitigation: rotate token regularly, monitor commit log via e-ink display on push.

---

## Device-side experience

### When server is enabled

- E-ink home screen shows a small icon: `📡` or similar.
- Settings → Git Server screen shows: status (running/stopped), URL, token (masked, "show/copy" button), QR code for the URL, last push timestamp + commit hash + author.

### During a push

- Brief banner on e-ink: "Receiving push from <user>…"
- Progress indicator if the push is large (>100 KB).
- On completion: "Pushed <commit-short-sha>: <commit-subject>" displayed for 5 seconds, then back to home.

### On push failure

- E-ink shows red error banner with one-line reason.
- Full error log accessible via Settings → Git Server → Recent Activity.

### Plugin reload visibility

- After a successful push that updates plugins, the plugin manager performs the reload and a brief "Plugins updated" toast appears.

---

## Phased rollout

### Phase 1 — Push only (MVP)

- C runtime: `crypto.sha1`, `zlib.inflate` (streaming), `http.on_post_stream`.
- Lua: `git_server`, `git_pack`, `git_objects`, `git_refs`, `git_pkt_line`, `git_auth`, `git_hooks`.
- Default post-receive hook with the standard auto-checkout mapping.
- Settings UI for enable/disable and token display.
- HTTPS-only, HTTP Basic auth.
- Single repo, single branch (any branch name works, but only one ref deploys).

### Phase 2 — Pull, branches, custom hooks

- `POST /git-upload-pack` for fetch/clone from the device.
- Multi-branch awareness in the default hook (e.g. only `main` deploys; `experimental` is a deployable branch only when active).
- User-overridable `post-receive.lua` shipped with pushes.
- Token rotation UI.
- Push-side QR-code-on-screen for first-time setup from a phone.

### Phase 3 — Quality of life

- Thin packs / delta compression in `upload-pack` for faster pulls.
- Optional periodic ref repacking offline.
- Push-options handling (`-o reload=false` to skip auto-reload).
- Per-branch deploy gating (Settings → Git Server → "Deploy from branch: <main>").
- Push-mirror to a remote (e.g. GitHub) on successful local push.

### Phase 4 — Speculative

- Signed-push verification (require Ed25519 commit signature with a known pubkey).
- Multiple repos.
- Web UI for browsing the on-device repo's log.

---

## Open questions

- **Does the X4 announce itself via mDNS as `x4.local`?** This is a UX detail that matters a lot for first-time setup. Either it does, or the user has to find the IP and use `https://192.168.x.y/repo.git`.
- **Should the bare repo live at `/.git/` (root) or `/.crosspoint/.git/`?** Putting it under `/.crosspoint/` is cleaner but means the runtime's auto-checkout target paths don't naturally match the dev's working tree paths. Putting it at `/` matches the dev's tree more naturally. Decision: tentatively `/.git/` at SD root, but revisit when the layout settles.
- **What's the max pack size we accept?** 50 MB feels generous for a plugin/translation repo. Configurable, but the default matters.
- **Should the post-receive hook be allowed to reject a push retroactively?** Hooks running after the push completes can't reject — the refs are already updated. Pre-receive hooks would be needed for that, which are out of scope for v1.
- **Conflict with the existing WebDAV / SFTP file management?** Both can coexist on the same SD card, but a WebDAV write between pushes could leave the runtime inconsistent with what git thinks is deployed. Recommend documenting "use one or the other, not both at the same time."
- **What happens if the device is reset mid-push?** Atomicity comes from the rename-after-fsync pattern on refs. Object writes are append-only. Worst case: orphaned objects in `/.git/objects/` (harmless; cleaned up offline) and a partial `incoming.pack` in tmp (deleted on next boot or next push).

---

## Why this fits the project

This feature is a particularly clean expression of the CrossLua Reader philosophy:

- **Tiny C surface** (3 new functions: `sha1`, `inflate`, `on_post_stream`) opens up an entire class of capability.
- **Most of the implementation is Lua** — updatable without reflashing, auditable in one repo, contributable as a plugin.
- **No new hardware requirements** — runs on the existing C3, uses the existing WiFi, uses the existing SD card.
- **Composes with existing features** — auto-checkout uses the existing plugin manager, the post-receive hook is just a Lua function, the UI lives in the existing Settings screen.
- **Improves the dev workflow for the maintainer and contributors** — pushes from a real editor with version history beats every other "deploy to an embedded device" pattern.
- **Atomicity, rollback, branches, history** — all things the device gets for free by speaking git's protocol, not by inventing its own deployment model.

The runtime ends up being able to host its own development environment, which is a satisfying property for a reader that's also designed to be a hackable Lua platform.
