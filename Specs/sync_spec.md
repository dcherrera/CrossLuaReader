# Sync Service Spec

A C-implemented background sync service that runs in firmware, receives files from companion apps over HTTP while the device is awake, writes them atomically to the SD card, and exposes a `sync.*` Lua API for plugins to observe and control it.

The motivation is one-tap file transfer: a user with a phone or laptop app should be able to push EPUBs, audiobooks, wallpapers, or any other content to the X4 without yanking the SD card, opening WebDAV, or running git. The companion app POSTs files; the device receives them in the background while the user reads.

This spec covers the runtime-side service only. Companion apps speak plain HTTPS and can be written in any language.

---

## Goals

1. **Background reception while reading.** Files arrive while the user is actively reading or browsing, with no visible disruption.
2. **First-class C subsystem.** Implementation lives in the firmware, not as a plugin. Always available, can't be unloaded, runs in its own FreeRTOS task.
3. **Atomic writes.** Files appear on the SD card whole or not at all. Partial uploads never corrupt user content.
4. **Lua-accessible.** All plugins can observe sync activity and react to new files via the `sync.*` Lua API.
5. **Companion-app agnostic.** The HTTP contract is documented; any client (Calibre plugin, desktop app, mobile share-sheet, CLI script) can implement it.
6. **Reliable across network drops.** Connection loss mid-upload triggers cleanup; the user can simply retry.
7. **Battery-aware.** WiFi-on cost is significant on a battery-powered device. Sync defaults to off; clear opt-in semantics.
8. **Composable with the rest of the runtime.** Doesn't conflict with the existing webserver, the future git server, or the dashboard pattern.

## Non-goals

- **Sync while the device sleeps.** The X4 sleeps to conserve power; WiFi is off in deep sleep. Sync only runs while the device is awake. (A periodic-wake mode is considered for a later phase.)
- **Two-way sync.** v1 is push-only (companion → device). Pulling user-on-device edits back to the companion is a Phase 3 question.
- **Conflict resolution semantics.** v1 treats every push as authoritative — last writer wins. No merge logic, no version vectors.
- **Multi-user.** One device, one shared token. No per-user accounts.
- **Cloud-style continuous sync.** This is a push-on-demand service, not a daemon that maintains a persistent diff with a remote folder. Companion apps can implement that pattern on top, but the device-side service is stateless about it.
- **Sync of plugins, translations, fonts.** Those go through the [git server](git_server_spec.md) for atomic versioned deploys. Sync is for end-user content (books, audio, wallpapers).

---

## Architecture

The sync service is a C-side subsystem with three layers:

```
┌────────────────────────────────────────────────────────────┐
│  C runtime (firmware)                                      │
│                                                            │
│  ┌──────────────────────────┐                              │
│  │ Sync FreeRTOS task       │  ◀── always alive when       │
│  │  - HTTP listener         │      service enabled         │
│  │  - Auth check            │                              │
│  │  - Streaming receiver    │                              │
│  │  - Atomic file writer    │                              │
│  │  - Event dispatcher      │                              │
│  └──────────┬───────────────┘                              │
│             │                                              │
│             ▼                                              │
│  ┌──────────────────────────┐                              │
│  │ sync.* Lua API surface   │  ◀── exposed to all plugins  │
│  │  enable() / disable()    │                              │
│  │  set_token() / set_root()│                              │
│  │  on_received(handler)    │                              │
│  │  list() / status()       │                              │
│  └──────────────────────────┘                              │
└────────────────────────────────────────────────────────────┘
                          ▲
                          │ Lua calls
                          │
┌────────────────────────────────────────────────────────────┐
│  Plugins (consume sync.*, none implement it)               │
│                                                            │
│  - file_browser: subscribes to sync.on_received,           │
│    refreshes its listing when files arrive                 │
│  - home: shows "N new books" badge from sync.list()        │
│  - settings: exposes UI for token, root path, enable       │
│  - dashboard: optional notification on receive             │
└────────────────────────────────────────────────────────────┘
```

The Sync task runs in its own FreeRTOS task, scheduled cooperatively with the existing display/input/plugin tasks. It owns its own TCP socket, its own buffers, and its own state. It does not share the Lua VM or compete with plugin execution for it.

When a file finishes uploading, the task fires a `sync_received` event. The runtime's event dispatcher invokes any Lua handlers registered via `sync.on_received(...)`, on the next safe scheduler tick. Plugins observe; they don't drive.

---

## Why C, not a Lua plugin

This is a deliberate architectural decision worth being explicit about, because the rest of the project favors fat Lua plugins on a thin C runtime.

**Sync is infrastructure, not policy.** The bytes-from-network-to-disk path is not a place where users want different implementations — they want one reliable implementation. Putting it in C:

- **Guarantees availability.** Plugin loading bugs, missing SD files, mid-transition unload races — none of them can disable sync. It exists in flash, it always runs.
- **Isolates the byte-pumping path.** A multi-MB file transfer doesn't have to traverse the Lua VM. No bytecode interpretation, no GC pressure, no Lua coroutine stacks. The C task moves bytes, computes a hash, writes to SD.
- **Lets it run as a true background task.** A Lua plugin runs on the active-plugin slot; only one plugin is active at a time. The sync service needs to run *concurrently* with the active reading plugin. That requires a separate FreeRTOS task, which is C-native.
- **Provides a clean API surface for plugins.** Any plugin can call `sync.on_received(...)` without depending on another plugin being installed. That's only possible if `sync.*` is part of the runtime, not a peer plugin.

The Lua-plugin layer above it is where policy lives: settings UI, sync history display, custom post-receive hooks. Those *do* benefit from being Lua and updatable. Plugins consume the C service; they don't reimplement it.

This pattern matches every other system module: `display.*`, `storage.*`, `font.*`, `input.*` are all C-implemented, Lua-exposed. Sync joins them.

---

## C runtime additions

A new `lib/sync/` subsystem in the C runtime, with a corresponding `lib/lua_api/api_sync.c/h` for the Lua bindings.

### Internal C surface

```c
// lib/sync/sync_service.h

typedef struct {
    bool enabled;
    char token[65];                    // 64 hex chars + null
    char root_path[64];                // default "/books"
    uint16_t port;                     // default 8090
    size_t max_file_size_bytes;        // default 50 MB
    bool reject_overwrites;            // default false
} sync_config_t;

void sync_init(void);                  // called from main.c at boot
void sync_start(const sync_config_t *cfg);
void sync_stop(void);
bool sync_is_running(void);
sync_status_t sync_get_status(void);   // idle | receiving | error

// Internal: called by HTTP receiver task on file completion
void sync_emit_received(const char *path, size_t bytes, const char *sha256);
```

### Lua-exposed API

Document under `docs/lua-api.md` in the same format as `display.*` and `storage.*`:

```
sync.enable()                          -- Start the service with current config
sync.disable()                         -- Stop the service, close the listener
sync.is_enabled() → bool

sync.set_token(token)                  -- Set or rotate auth token (32+ bytes hex)
sync.get_token() → string              -- Read current token (for display in UI)
sync.regenerate_token() → string       -- Make a new random token, return it

sync.set_root(path)                    -- Default destination dir, e.g. "/books"
sync.get_root() → string

sync.set_max_file_size(bytes)          -- Reject files larger than this
sync.set_port(port)                    -- HTTP port, default 8090

sync.status() → "idle" | "receiving" | "error" | "disabled"
sync.list([limit]) → table             -- Recent received files: { {path, bytes, time, sha256} }
sync.clear_history()

sync.on_received(handler)              -- handler(path, bytes, sha256, source_ip)
sync.off_received(handler)
```

### Configuration persistence

All config goes into NVS, keyed under `sync.*`. Loaded on boot. Survives reboots. The token specifically is generated on first enable if absent, never logged, never serialized to anywhere but NVS.

---

## HTTP routes (the wire contract)

The sync service listens on its own port (default 8090, configurable) — separate from the main webserver to avoid interleaving with WebDAV / existing routes. All routes require authentication.

### Authentication

Every request must include `Authorization: Bearer <token>`. The token is checked in constant time against the stored value. Failures return `401 Unauthorized`.

After 5 consecutive auth failures, the service locks out new requests for 60 seconds. After 30 failures in 10 minutes, an alert appears on the e-ink screen.

### `POST /sync/upload`

Upload a single file.

**Headers:**
- `Authorization: Bearer <token>` — required
- `X-Sync-Path: <relative-path>` — required, the path under the root (e.g. `new-novel.epub`)
- `X-Sync-SHA256: <hex>` — optional, expected SHA-256 of the file content
- `X-Sync-Overwrite: true|false` — optional, defaults to `true`
- `Content-Length: <bytes>` — required
- `Content-Type: application/octet-stream`

**Body:** raw file bytes.

**Behavior:**
1. Validate auth, path, content-length (reject > `max_file_size_bytes`).
2. Open a temp file at `<root>/.sync/<basename>.partial`.
3. Stream the body to the temp file, computing SHA-256 as it goes.
4. If `X-Sync-SHA256` was supplied and doesn't match the computed value: delete temp, return `422 Unprocessable Entity`.
5. If the destination exists and `X-Sync-Overwrite: false`: delete temp, return `409 Conflict`.
6. If the destination is the *currently open book* in the reader: delete temp, return `423 Locked`.
7. Atomically rename the temp file to `<root>/<X-Sync-Path>`.
8. Fire the `sync_received` event with the path, byte count, and SHA-256.

**Response (success):**
```json
{
  "ok": true,
  "path": "new-novel.epub",
  "bytes": 482113,
  "sha256": "a1b2c3...",
  "received_at": 1714400000
}
```

**Response (failure):** standard HTTP status + JSON `{ "ok": false, "error": "..." }`.

### `GET /sync/manifest`

Return the device's current view of synced content, for companion-app diff computation.

**Query params:**
- `path` — optional, restrict to a subdirectory. Defaults to root.
- `recursive` — optional, defaults to `true`.

**Response:**
```json
{
  "root": "/books",
  "files": [
    { "path": "novel-a.epub", "bytes": 482113, "sha256": "abc...", "mtime": 1714000000 },
    { "path": "audiobooks/podcast.mp3", "bytes": 9012345, "sha256": "def...", "mtime": 1714010000 }
  ]
}
```

The companion app diffs this against its local view to decide what to push or delete. SHA-256s are computed lazily and cached on first read.

### `DELETE /sync/file`

Remove a file by path.

**Headers:**
- `Authorization: Bearer <token>`
- `X-Sync-Path: <relative-path>`

**Behavior:**
1. Refuse if the file is the currently open book (`423 Locked`).
2. Delete the file.
3. Fire a `sync_deleted` event.

### `GET /sync/status`

Lightweight health check for companion apps.

**Response:**
```json
{
  "ok": true,
  "version": "1.0",
  "device_name": "X4-bedroom",
  "free_space_bytes": 7340032000,
  "battery_percent": 73,
  "is_busy": false,
  "current_book": "novel-a.epub"
}
```

### `POST /sync/batch`

Optional convenience: upload a `.zip` containing multiple files. Useful for first-time sync of a large library.

**Behavior:** unzip into root, fire `sync_received` for each file extracted. Cap on archive size and per-file size enforced.

This is a Phase 2 route — not required for v1 but called out so the URL space is reserved.

---

## SD card layout

```
/                           SD root
├── books/                  Default sync root
│   ├── novel-a.epub        User content
│   ├── audiobooks/
│   │   └── podcast.mp3
│   └── .sync/              Sync staging directory
│       └── *.partial       In-flight uploads, deleted on failure
├── plugins/                Untouched by sync (managed via git server)
├── languages/              Untouched by sync
├── fonts/                  Untouched by sync
└── .crosspoint/            Existing cache directory
```

The sync service refuses to write outside the configured root. Any `X-Sync-Path` containing `..` or starting with `/` is rejected with `400 Bad Request`.

The `.sync/` staging directory is created on first run. Stale `.partial` files left by interrupted uploads are cleaned up on service start.

---

## Receive flow (step by step)

The exact sequence the C task runs for each upload:

1. **Accept TCP connection.** Server socket is bound and listening; accept yields a per-connection socket.
2. **Parse HTTP request line + headers.** Standard incremental parsing. Reject malformed input early with `400`.
3. **Authenticate.** Constant-time compare of `Authorization` token. On failure: increment counter, `401`, close.
4. **Validate path.** No `..`, no absolute, fits within `root`. Computed final destination is recorded.
5. **Check size.** `Content-Length` must be present and ≤ `max_file_size_bytes`.
6. **Lock check.** If the destination matches the currently-open book (queried via plugin manager state), reject with `423 Locked`.
7. **Open temp file.** `<root>/.sync/<basename>-<rand8>.partial`.
8. **Stream loop:**
   - Read up to 4 KB from socket.
   - Update SHA-256 context.
   - Write to temp file.
   - Update progress counter (for status reporting).
   - Yield to scheduler periodically so the active plugin keeps rendering.
9. **Verify SHA** if the client supplied `X-Sync-SHA256`.
10. **Verify byte count** matches `Content-Length`.
11. **Atomic rename.** `rename(temp_path, dest_path)`. POSIX-atomic on most filesystems; fall back to delete-then-rename if needed.
12. **Fire `sync_received` event** to the Lua dispatcher.
13. **Append to recent-files history** (capped, ring buffer in NVS).
14. **Send response.** JSON success body, 200 OK.
15. **Close socket.**

If any step after the temp file is open fails, the temp file is unlinked. The destination is never touched.

---

## Display contention with active rendering

The X4's e-ink panel and SD card share the SPI bus. Concurrent SD writes during a page render cause both to stutter. The sync service mitigates this:

- Before each chunk write to SD, the sync task checks a global `is_rendering` flag (set by the renderer for the duration of an active update).
- If `is_rendering` is true, the sync task waits up to 200 ms for it to clear before writing.
- If still rendering after 200 ms, writes the chunk anyway — page renders complete eventually, and indefinite wait could starve the upload.

This keeps interactive page-turn latency unaffected for ~95% of cases. The remaining 5% see a barely-perceptible delay.

A `sync.set_priority("low" | "normal" | "high")` API lets advanced users tune this — `low` waits longer (better reading experience, slower sync), `high` waits less (faster sync, occasional render glitches).

---

## Battery and power management

WiFi-on draws ~80-100 mA versus ~20 mA for reading-only. Sync's network listener requires WiFi-on whenever enabled. This roughly quarters battery life.

### Default behavior

- **Sync is disabled by default.** Out-of-the-box experience is reading-only with WiFi off.
- **Settings → Sync → Enable** turns it on.
- Once enabled, WiFi stays on while the device is awake.
- Sync is suspended automatically during deep sleep (WiFi off).

### Power-saving modes (Phase 2)

- **Charger-only mode.** Sync only runs while a USB charger is connected.
- **Battery threshold.** Sync auto-disables below 20% battery.
- **Wake-and-poll.** Device sleeps with WiFi off, wakes every N minutes (configurable: 5/15/60), connects, polls a configured server for new files via `GET`, sleeps again. Higher latency, much lower idle power. This requires the companion app to host an outbound-pull endpoint, which not all setups will have.
- **MQTT-trigger.** Device subscribes to an MQTT topic during light sleep; a publish wakes it to fetch. Lowest latency, modest power overhead. Depends on the MQTT capability landing.

### Status visibility

The home screen shows a small WiFi/sync indicator when the service is enabled:
- Solid icon: enabled, idle
- Animated icon: receiving
- Red icon: enabled but error (e.g., last upload failed)

Tapping a side-button hint enters the Sync status screen.

---

## Settings UI

The sync settings live in a dedicated `sync_settings.lua` plugin — this is the Lua layer where policy lives. The plugin uses `sync.*` to read/write config and renders the standard settings UI.

```
Settings → Sync
─────────────────────────────────────
  ☐ Enable Sync                    ▶
  Sync uses WiFi and reduces battery
  life. Default: off.

  Connection
  ─────────────────────────────────
  URL          https://x4.local:8090/sync
  Token        a1b2c3d4… [Show] [Rotate]
  QR Code      [Display]                ▶
  Port         8090                     ▶

  Storage
  ─────────────────────────────────
  Root folder  /books                   ▶
  Max file     50 MB                    ▶
  Free space   7.3 GB

  Power
  ─────────────────────────────────
  ☐ Pause sync below 20% battery
  ☐ Sync only while charging
  Priority     Normal                   ▶

  History
  ─────────────────────────────────
  Recent files (last 10)                ▶
  [Clear history]
```

The "QR Code" submenu displays a QR encoding the full URL with embedded token, for one-tap setup from a phone scanning the screen.

---

## Companion app contract

Any companion implements this minimal protocol:

1. **Discovery** — usually `x4.local` via mDNS, or manually configured IP.
2. **Authentication** — Bearer token, scanned from QR code or copy-pasted.
3. **Probe** — `GET /sync/status` to verify the device is alive and reachable.
4. **Diff** — `GET /sync/manifest` returns the device's content. Companion compares against its local watched folder.
5. **Push** — for each new or changed file, `POST /sync/upload` with `X-Sync-Path` and `X-Sync-SHA256`.
6. **Delete** (optional) — for each file removed locally, `DELETE /sync/file`.

That's it. Anyone can implement this in any language. Reference companions to ship with the project:

- **Calibre plugin** — adds X4 as a "device" so Calibre's existing send-to-device flow works out of the box.
- **CLI tool** — `x4sync push ~/Books/` for terminal users and cron jobs.
- **Tauri desktop app** — folder-watch + drag-drop UI for non-technical users.
- **Mobile share-sheet integration** — Android intent + iOS share extension to "Send to X4."

These live in a separate `companions/` repo; they're not part of the firmware.

---

## Security considerations

### Threat model

- Adversary on the same WiFi network or the device exposed to the open internet.
- Goals: prevent unauthorized writes, prevent unauthorized reads of sensitive content (notes, books), prevent denial-of-service.

### Mitigations

- **HTTPS only.** Plain HTTP returns `426 Upgrade Required`. Self-signed cert acceptable; companion apps add the cert to their trust store on first connect.
- **Bearer token, 32 random bytes.** Stored in NVS only. Constant-time comparison.
- **Rate limit on auth failures.** 5 fails → 60 s lockout. 30 fails / 10 min → e-ink alert.
- **Bounded resource usage.** Reject files > `max_file_size_bytes`. Reject batches > 200 MB. Reject paths > 256 bytes.
- **Path sandboxing.** No `..`, no absolute paths, no symlinks. Final write path must canonicalize within `root`.
- **No execution.** Received files are bytes on disk. The sync service never `dofile`s, never `loadstring`s, never auto-loads received content. (Plugins receiving sync events choose their own behavior, but the C service itself is inert.)
- **Concurrent-write protection.** A file currently open by the active reader is locked from sync overwrite.

### Out of scope for v1

- Per-path access control (every authenticated client has full access to root).
- Signed pushes / publisher verification.
- End-to-end encryption beyond TLS.

These can be revisited in Phase 3 if the threat model expands.

---

## Memory and performance budget

| Resource | Cost |
|----------|------|
| Sync FreeRTOS task stack | 4 KB |
| HTTP receive buffer | 4 KB |
| SHA-256 context | ~200 bytes |
| Auth state (token, lockout counter) | ~100 bytes |
| Recent-files history (16 entries × ~80 bytes) | ~1.5 KB |
| TCP socket | ~3 KB (lwIP) |
| **Total RAM (idle, enabled)** | **~13 KB** |
| **Total RAM (during a transfer)** | **~15 KB** |

Comfortably under the 380 KB ceiling, even with WiFi (50 KB) also active. Significantly cheaper than a Lua-plugin implementation would be.

| Operation | Time |
|-----------|------|
| Auth check | <5 ms |
| Status response | <50 ms |
| Manifest of 100 files | ~500 ms (SHA cached) |
| Manifest of 100 files (uncached) | ~5-15 s (SHA computation dominates) |
| Upload of 1 MB file | 5-15 s (WiFi-bound) |
| Upload of 10 MB file | 30-90 s |
| Atomic rename | <50 ms |

Bottleneck for first-time large library sync is WiFi throughput, typically 1-3 MB/s in practice for the C3.

---

## Phased rollout

### Phase 1 — Minimum viable sync (target this)

- C-side `sync.*` module with `enable`, `disable`, token management, root path.
- HTTP routes: `POST /sync/upload`, `GET /sync/manifest`, `GET /sync/status`, `DELETE /sync/file`.
- Atomic write via temp + rename.
- Lock-out for currently-open book.
- Lua API surface, fully documented in `docs/lua-api.md`.
- Settings plugin (`sync_settings.lua`) for enable, token rotation, root selection.
- File browser plugin subscribes to `sync.on_received` to refresh.
- Calibre plugin as the first companion.

### Phase 2 — Quality of life

- `POST /sync/batch` for zip-of-files first-time sync.
- Power-saving modes: charger-only, battery threshold, wake-and-poll.
- QR code generator for one-tap mobile setup.
- Sync history UI with file thumbnails (for cover art).
- Conflict reporting (file changed since manifest fetch).
- mDNS announcement so `x4.local` works without manual IP.

### Phase 3 — Bidirectional and remote

- Two-way sync — on-device notes/annotations push back to companion.
- MQTT-trigger sync — wake from sleep on push notification.
- Remote sync over Cloudflare Tunnel — public-internet companions.
- Multi-root support — sync different content to different SD locations.
- Per-path tokens — limit a token to specific subtrees.

### Phase 4 — Speculative

- Delta uploads — patch existing files instead of full replacement (rsync-style).
- Auto-conversion pipeline — companion sends a Word doc, device converts and stores as EPUB.
- Sync of plugin/translation packs as an alternative to the git server, for non-developer users.

---

## Open questions

- **Should sync's HTTP listener live on the same port as the main webserver, or its own port?** Spec currently says separate port (8090). Pro: cleaner separation, independent enable/disable, no route conflicts. Con: companion app needs to know two ports if it also uses the main webserver. Tentative answer: separate port.
- **Should we support resume on interrupted uploads?** With multi-MB transfers and WiFi flakiness this could matter. Implementation cost: a `Range`-style protocol on retry, plus checkpointing partial SHA state. Probably defer to Phase 2.
- **What happens when the SD card is full?** Current plan: `507 Insufficient Storage`, surface visibly on the e-ink screen. Companion app should fall back to deferring further uploads. Worth specifying the exact error contract.
- **Should sync history persist across reboots?** Yes for the most recent ~16 entries (NVS-stored). Older history lives in a `~/.crosspoint/sync.log` on the SD card if the user wants it. Decision pending.
- **mDNS vs static IP for v1?** mDNS via the underlying esp_netif is straightforward to enable and dramatically improves first-run UX (`x4.local` vs `192.168.1.143`). Decision: include in Phase 1.
- **Token format — opaque random or signed JWT?** Opaque random (32 bytes hex) for simplicity. JWTs add no security here and require crypto for signature verification. Decision: opaque.
- **Concurrency — how many simultaneous uploads do we allow?** v1: one at a time. Multiple connections queue. Phase 2 may revisit if useful.

---

## Why this fits the project

The sync service is a particularly clean expression of the project's architectural pattern, *now that we've correctly placed it on the C side:*

- **Tiny, focused C surface.** A `sync.*` module is one cohesive subsystem, not a sprawl. ~1500-2000 lines of C, well-bounded, testable.
- **Lua surface where Lua belongs.** Settings UI, history display, custom event handlers — all Lua plugins, all updatable.
- **Reuses existing infrastructure.** WiFi stack, SD I/O via HAL, NVS for config, the FreeRTOS task model.
- **Composes with the rest of the runtime.** The git server handles plugin/translation deploys; sync handles user content. Both speak HTTP. Both honor the same auth pattern. Both fire events plugins can react to.
- **Sets a template for future C-modules-with-Lua-surfaces.** The same shape applies to the eventual MQTT client, dashboard event router, OTA firmware updater, etc. Each one is a focused C subsystem with a clean Lua API surface for plugin composition.

Once this lands, the device's "I am a thing on your network that you can push content to" story becomes a built-in capability, not a feature you have to install. That's the right place for it.
