# CrossLua Kernel — Long-Term Plan

**Status:** Internal architecture document.
**Audience:** Project maintainers and informed contributors.
**Goal:** Crystallize the framing, scope, and roadmap for evolving the CrossLua runtime into a proper kernel for e-paper devices.

---

## 1. Framing

### What this project actually is

CrossLua is a **microkernel-style runtime for single-user, single-foreground-task e-paper devices.** Not Linux, not POSIX, not a general-purpose OS. The closest conceptual peers:

- **PebbleOS** — small kernel + JS/C apps for a constrained smartwatch
- **PICO-8 / TIC-80** — "fantasy console": tiny C engine + fixed Lua API, apps shipped as cart files
- **PalmOS** — small kernel + apps in a constrained environment

The PICO-8 analog is the cleanest. PICO-8 is a small C engine exposing a fixed Lua API, runs cart files as "apps," and has a passionate community shipping carts because the platform is small and clear. CrossLua is the e-reader version of that thesis.

### Kernel concept mapping

| Kernel concept | CrossLua equivalent |
|---|---|
| Hardware abstraction layer | `lib/hal/` |
| Driver model | HAL bridge files (`bridge_*.cpp`) |
| System call surface | Lua C API (`lib/lua_api/api_*.c`) |
| Process model | Plugin = process, Lua state = address space |
| IPC | Event bus (planned) |
| Scheduler | Task/coroutine scheduler (planned) |
| Memory manager | Per-plugin Lua heap caps + shared C-side resource cache (planned) |
| Capability system | Phase 9.0 opt-in capability registration |
| Boot/init | `src/main.c` |
| Userspace | SD card `.lua` plugins |

### Naming

Internal name: **CrossLua Kernel.** External naming remains "CrossLua Reader" / "CrossLua firmware" / "CrossLua runtime" — the kernel framing is a maintainer's mental model, not a marketing pivot.

**Plugin artifact naming:** plugins ship as `.ticl` files (TeamIDE CrossLua), referred to verbally as "tickles." Format and rationale documented in section 5.

### Guiding principles

1. **We do not break userspace.** Once a Lua API is marked stable, plugins targeting that API must continue to work across kernel versions. Deprecation requires a notice cycle.
2. **Anything that can be Lua should be Lua.** C exists for hardware access, performance, and policy enforcement. Everything else is userland.
3. **The syscall table is designed, not evolved.** New Lua APIs land deliberately, with documentation, capability requirements, and stability classification.
4. **Minimal is a means, not an end.** Optimize for plugin developer experience and end-user reliability. Flash budget is generous (~10% used of 16MB); spend it on services that multiply plugin capability.
5. **Hardware portability via build flags.** One codebase, multiple HAL bridge sets selected at compile time. The kernel ABI is identical across boards.

---

## 2. Architecture

### Layer model

```
+---------------------------------------------------------+
|                    Userland (SD Card)                   |
|   Lua plugins: readers, services, activities, tools     |
+---------------------------------------------------------+
|                    Kernel ABI (syscall surface)         |
|   display.*  input.*  storage.*  http.*  audio.*        |
|   task.*     events.* kv.*       i18n.*  ui.*           |
+---------------------------------------------------------+
|                  Kernel Services (C)                    |
|   Plugin manager     Scheduler     IPC bus              |
|   Memory caps        Capability gate                    |
|   Renderer           Font system   BiDi                 |
|   KV store           Crash log     Settings             |
+---------------------------------------------------------+
|                      HAL (C)                            |
|   hal_display  hal_gpio  hal_storage  hal_power         |
|   hal_system   hal_audio  hal_net                       |
+---------------------------------------------------------+
|                  HAL Bridges (C++)                      |
|   bridge_display.cpp     bridge_input.cpp               |
|   bridge_storage.cpp     bridge_battery.cpp             |
|   (selected per build target via PlatformIO env)        |
+---------------------------------------------------------+
|                  Hardware (X4 / Waveshare / ...)        |
+---------------------------------------------------------+
```

### Kernel vs userland boundary

**Stays in C (kernel):**
- HAL and drivers (hardware access)
- Lua interpreter and scheduler
- Renderer, font system, BiDi (performance-critical, called constantly)
- Plugin lifecycle, capability gate, memory accounting
- Network stack (TLS, HTTP), audio pipeline (decoders, BT routing)
- KV store backend, crash logging, structured logging
- System UI primitives (notifications, modals, status bar compositor)

**Lives in userland (Lua, on SD):**
- File-format readers (EPUB, TXT, MD, PDF metadata, etc.)
- File browser, settings UI, language picker
- Network features (Calibre sync, OPDS, RSS, etc.)
- Games and toys
- Translations (`/languages/<code>/lang.json`)
- Themes (potentially — under discussion)

**Debatable (could be either):**
- ZIP/XML/JSON parsers — currently C for performance, could become optional registered modules per build
- Date/time formatting — likely C (used in many places)
- Crypto helpers — C (security-sensitive)

---

## 3. The Syscall Surface

The Lua API is the kernel ABI. Every function plugins call falls into one of these namespaces.

### Existing namespaces (Phase 1–9.0)

| Namespace | Responsibility |
|---|---|
| `display.*` | Drawing primitives, framebuffer commit, refresh, dimensions |
| `input.*` | Button events, button state |
| `storage.*` | File I/O on SD (currently unsandboxed) |
| `wifi.*` | Connect, disconnect, status |
| `font.*` | Font loading, text measurement |
| `system.*` | Heap stats, restart, sleep, time |

### Planned namespaces

| Namespace | Responsibility |
|---|---|
| `task.*` | Cooperative task scheduling (coroutine-backed, but coroutines are not exposed to plugins) |
| `events.*` | Pub/sub event bus for plugin-to-plugin and kernel-to-plugin signaling |
| `kv.*` | Key-value store for plugin settings and small state |
| `http.*` | Async HTTP client with TLS |
| `audio.*` | Audio playback (file → decode → BT/I2S routing) |
| `ui.*` | System UI primitives (notify, confirm, alert, statusbar) |
| `i18n.*` | Localization API: `i18n.t("key")`, locale selection |
| `theme.*` | Color tokens, dark/sepia/light mode |
| `time.*` | RTC, NTP, scheduling, calendar math |
| `log.*` | Structured logging (`log.info(module, msg, fields)`) |
| `net.*` | Beyond HTTP: connection state, mDNS, offline queue |

### Stability classification

Every Lua API function carries a stability tag:

- **stable** — covered by the no-break-userspace contract
- **experimental** — may change between kernel minor versions
- **deprecated** — slated for removal in a future major version; warning logged on use

A plugin manifest declares `runtimeVersion` and the kernel rejects plugins requesting APIs not present in their declared version.

---

## 4. System Services to Build

In rough priority order. Each item lists scope, why it matters, and rough effort.

### 4.1 Per-plugin storage sandbox + KV store

**What:** `storage.dataDir()` returns a per-plugin sandboxed path. `kv.get(key)` / `kv.set(key, value)` for small typed state. Atomic write helper for power-loss safety.

**Why:** Prevents plugins stepping on each other. Stops every plugin from reinventing JSON-on-SD for settings. Atomic writes prevent corruption when battery dies mid-save.

**Effort:** ~1 week. Backed by LittleFS or a flat append-only log.

### 4.2 Per-plugin Lua memory cap + onLowMemory event

**What:** `lua_setallocf` hook enforcing a per-plugin Lua heap ceiling. When 80% threshold hit, fire `onLowMemory()` to the plugin so it can drop caches. Past the cap, allocator returns NULL and Lua raises a recoverable error.

**Why:** One bad plugin must not OOM the device. Bounded freedom requires bounded resource use.

**Effort:** 2 days.

### 4.3 IPC event bus

**What:** `events.publish(name, data)` and `events.subscribe(name, fn)`. Kernel publishes system events (battery, power, network, time, sd, input). Plugins publish their own events for cross-plugin coordination.

**System events to publish from C:**
- `battery.low`, `battery.critical`, `battery.charging`
- `power.sleep`, `power.wake`
- `network.up`, `network.down`
- `sd.inserted`, `sd.removed`
- `time.tick` (every minute)
- `display.refreshComplete`
- `input.button` (raw events for plugins that opt in)

**Why:** Without an event bus, every plugin polls or reimplements ad-hoc state. Event bus is the IPC primitive that makes service plugins useful.

**Effort:** ~1 week. ~150 lines of C for in-memory pub/sub.

### 4.4 Service plugin lifecycle

**What:** Service-type plugins survive activity switches. They get `onStart`, `onStop`, `onTick(dt)` instead of `onEnter`/`loop`/`onExit`. Bounded count of concurrent services. No UI; communicate via events and `ui.notify`.

**Why:** Audiobook playback, sync daemons, clock widgets, network discovery — none of these are possible without long-lived services. This is the difference between "plugins are apps" and "plugins are an OS."

**Effort:** ~1 week. Includes scheduler integration.

### 4.5 Async HTTP + TLS

**What:** `http.get(url) → response` (yields under the hood). Connection pooling. Response object with status, headers, body. `http.post`, `http.put`, etc.

**Why:** Every modern feature needs HTTP. TLS is non-negotiable in 2026. Without this, no Calibre sync, no OPDS, no API integrations, no software updates.

**Effort:** ~2 weeks. mbedTLS adds ~80–150 KB.

### 4.6 System UI toolkit

**What:** Kernel-rendered UI primitives that plugins request:
- `ui.notify(text, [icon], [duration])` — toast, auto-dismisses
- `ui.confirm(text, callback)` — modal yes/no/cancel
- `ui.alert(text, callback)` — modal OK
- `ui.statusbar.set({ battery, time, wifi, custom })` — kernel composites
- `ui.progress(text, fraction)` — modal progress indicator

**Why:** Plugins should not reimplement scroll bars, dialogs, and toasts. Consistency improves UX. Kernel-side rendering means modals work over any plugin's screen.

**Effort:** ~2 weeks. Integrates with damage tracking.

### 4.7 Damage tracking / partial refresh manager

**What:** Kernel tracks dirty rectangles. Plugins draw to framebuffer; the refresh manager picks full or partial refresh based on dirty area + ghosting heuristics. Partial refresh ~0.3s, full ~1.5s on typical e-ink.

**Why:** This is what makes UI feel snappy on e-ink. Without it, every change triggers a full refresh and the device feels sluggish.

**Effort:** ~1 week.

### 4.8 Coroutine-backed task scheduler

**What:** `task.spawn(fn)`, `task.delay(ms, fn)`, `task.wait(ms)`, `task.cancel(id)`. Coroutines used internally by the kernel; not exposed to plugins. Async kernel functions yield internally and resume when work completes.

**Why:** Without async, HTTP/audio/file ops freeze the UI. Coroutines hidden behind `task.*` is the canonical Lua idiom (Roblox, OpenResty, neovim plenary, LÖVE all use this pattern). The original concern of "plugin authors don't use coroutines" is correct — they shouldn't have to. The kernel uses them; plugins write linear-looking code.

**Effort:** 3 days for the scheduler. Coroutines must be re-enabled in the Lua build.

### 4.9 Crash log + structured logging

**What:**
- Lua errors caught by `lua_pcall` are persisted to `/system/crashes/<timestamp>.log` with stack trace, plugin id, runtime version, free heap.
- `log.info(module, msg, fields)` writes structured records to serial AND a rotating SD log.
- Crash log viewable from a settings screen.

**Why:** Without persistent crash logs, plugin authors can't debug field issues. Structured logging makes log analysis tractable.

**Effort:** 3 days.

### 4.10 Desktop simulator

**What:** SDL2-based desktop build of the kernel. HAL stubs back onto SDL surface, keyboard maps to buttons, local filesystem maps to SD root. Plugin authors run `crosslua-sim ./my_plugin` and iterate without flashing.

**Why:** This is the single biggest accelerator for plugin development. Flash-and-test cycles take 30+ seconds; SDL iteration is sub-second. Every successful constrained-platform community (Pebble, Playdate, PICO-8) had a simulator. Without one, the plugin ecosystem grows slowly.

**Effort:** ~2 weeks. Architecturally this validates the HAL abstraction.

### 4.11 Settings manager + plugin manifest

**What:**
- Plugins declare `plugin.settings = { brightness = { type="int", min=0, max=100, default=50 }, ... }`.
- Kernel auto-renders a settings UI for each plugin from its schema.
- `settings.get(key)` / `settings.set(key, value)` with type coercion.
- Settings changes broadcast via `events.publish("settings.changed", { key, value })`.
- Plugin manifest expanded to include version, author, license, runtimeVersion, capabilities, settings schema.

**Why:** Removes per-plugin settings UI duplication. Manifest data drives the plugin browser, version compatibility, and capability auditing.

**Effort:** ~3 days.

### 4.12 Time and scheduling services

**What:**
- RTC abstraction (uses on-chip RTC if present, NTP-synced).
- `time.now()`, `time.utcNow()`, formatted output.
- `time.scheduleAt(timestamp, callback)` — plugin gets a callback at wall-clock time.
- Wake-on-timer for deep sleep: `system.wakeAt(timestamp)`.
- Cron-style recurrence helper.

**Why:** Alarms, scheduled downloads, daily quotes, sleep tracking, reading-streak tracking — all need time as a service.

**Effort:** ~3 days (excluding NTP, which lives in network stack).

### 4.13 Audio pipeline (gated on hardware)

**What:**
- `audio.play(path, options)`, `audio.pause()`, `audio.seek()`, `audio.position()`.
- Kernel handles file → decoder → SBC → BT or → I2S DAC routing.
- Decoders: Helix MP3, libfaad-tiny for AAC. Optional Vorbis later.
- Runs as a kernel service, survives plugin switches.

**Why:** Audiobooks. Only viable on Original ESP32 (not C3 — no BT Classic). This is why the Waveshare port is not just "different hardware" but a feature unlock.

**Effort:** ~2–3 weeks. Substantial. Gated on hardware target.

---

## 5. Plugin Protocols

The contracts plugin authors implement. These are the syscall ABI from the kernel's perspective and the lifecycle from the plugin's.

### Plugin file format and naming

**Extension:** `.ticl` (TeamIDE CrossLua).
**Verbal noun:** "tickle" (singular), "tickles" (plural). A plugin is "a tickle." Writing one is "tickling." A finished plugin is "a shipped tickle."

This naming is intentional — every successful constrained-platform community has a noun for its artifact (PICO-8 carts, Playdate games, Pebble apps, Roblox places). "Tickle" gives plugin authors and users a single word to anchor identity, marketing, and community vocabulary around (`#tickles` channel, "Tickle Jam" events, "tickle of the week," etc.).

**Format:**

A `.ticl` file is one of two things, detected by reading the first 4 bytes:

1. **Plain Lua source** (UTF-8 text). Used for simple single-file plugins. The kernel feeds the contents directly to `luaL_loadstring`.
2. **Bundle** (ZIP archive — magic bytes `PK\x03\x04`). Used for plugins with assets, translations, or multi-file structure. Layout:
   ```
   my_plugin.ticl  (zip)
   ├── main.lua             (entry point — required)
   ├── manifest.lua         (or embedded in main.lua's `plugin = {...}` — required either way)
   ├── assets/              (optional images, sounds)
   ├── lang/                (optional per-plugin translations)
   └── fonts/               (optional bundled .cfont files)
   ```

Format detection happens at load time; plugin authors writing v1-style plain `.ticl` files keep working without changes when bundles ship.

**Loose `.lua` files on the SD card are ignored** by the plugin manager. This prevents accidental loading of editor backups, library files, or test scripts. Only `.ticl` is recognized.

**Editor support:**

Most editors won't auto-detect `.ticl` as Lua. Three handling options for plugin authors:

1. **VS Code extension** (planned, ~50 lines of JSON) — registers `.ticl` → Lua syntax highlighting and adds a quick-validate command.
2. **Modeline directive** — first line `-- vim: ft=lua` works for vim/neovim out of the box.
3. **Edit as `.lua`, ship as `.ticl`** — plugin authors using less-configurable editors can rename while editing and rename back when shipping.

### Plugin lifecycle (all plugins)

```lua
plugin.onEnter(arg)       -- activated
plugin.loop()             -- per-frame (activities only)
plugin.onPause()          -- another plugin took foreground (services only)
plugin.onResume()         -- regained foreground (services only)
plugin.onExit()           -- deactivated
plugin.onLowMemory()      -- kernel asking for cache release
plugin.onSettingsChanged(key, value)  -- a setting changed
```

### Reader plugin protocol

```lua
plugin.fileExtensions       -- {"epub", "epub3"}
plugin.canOpen(path)        -> bool
plugin.open(path)           -> bool
plugin.renderPage(pageNum)
plugin.getPageCount()       -> int
plugin.getProgress()        -> float (0.0–1.0)
plugin.goToProgress(p)
plugin.getMetadata()        -> { title, author, ... }
```

### Service plugin protocol

```lua
plugin.onStart()            -- kernel started this service
plugin.onStop()             -- kernel stopping this service
plugin.onTick(dt)           -- periodic work
-- services use events.publish to communicate; have no draw surface
```

### Manifest

```lua
plugin = {
    id = "my_plugin",
    name = "My Plugin",
    version = "1.0.0",
    author = "Your Name",
    license = "MIT",
    runtimeVersion = "0.5",       -- minimum kernel ABI version
    type = "activity",             -- activity | reader | service
    menuEntry = "My Tool",
    capabilities = {"storage", "wifi"},   -- declared upfront, gated by kernel
    settings = { ... },            -- schema for auto-rendered settings UI
    fileExtensions = { ... },      -- readers only
}
```

---

## 6. ABI Stability and Versioning

### Kernel version scheme

Semantic-ish, adapted for kernel ABI:

- **Major (1.x → 2.x):** Allowed to break stable APIs. Plugins must be updated.
- **Minor (1.0 → 1.1):** Adds new APIs. Existing stable APIs unchanged. May change `experimental` APIs.
- **Patch (1.0.0 → 1.0.1):** Bug fixes only. No API changes.

### Stability tagging

Each Lua-callable function has one of:
- `stable` — protected by no-break-userspace
- `experimental` — may change in any minor release; documented as such
- `deprecated` — present but warns on call; removed in next major

### Plugin ABI declaration

Plugins declare `runtimeVersion = "0.5"` in their manifest. On load:
- Kernel verifies all APIs the plugin uses exist at that version
- If a stable API changed shape (which would be a major-version event), kernel rejects with a clear error
- If runtimeVersion is older than current and APIs are unchanged: load normally
- If runtimeVersion is newer than current: reject with "update your firmware"

### Deprecation cycle

1. Mark function `deprecated` in source. Kernel logs a warning when called.
2. Document replacement in API reference.
3. Remove in next major version, with at least one minor release of warning.

---

## 7. Hardware Portability

### Target boards (planned)

| Board | Status | Notes |
|---|---|---|
| Xteink X4 (ESP32-C3) | Primary | Reference platform |
| Waveshare ESP32 driver board + 7.5" panel | Planned | DIY hardware path; unlocks BT audio |
| Other ESP32 e-paper boards | Future | Same pattern as Waveshare |

### Build-flag-driven targets

PlatformIO multi-env structure:

```ini
[env:x4]
platform = espressif32
board = esp32-c3-devkitm-1
build_flags = -DPLATFORM_X4
build_src_filter = +<*> -<hal/bridge_*_waveshare.cpp>

[env:waveshare]
platform = espressif32
board = esp32dev
build_flags =
    -DPLATFORM_WAVESHARE
    -DBTN_BACK_PIN=12
    -DBTN_CONFIRM_PIN=14
    -DBTN_NEXT_PIN=27
    -DEPD_CS_PIN=5
    -DEPD_DC_PIN=17
build_src_filter = +<*> -<hal/bridge_*_x4.cpp>
```

Pin maps for buttons / SPI / SD passed via `-D` flags so a single `bridge_input_gpio.cpp` works across boards.

### Per-board feature gating

Some kernel services depend on hardware capability:
- BT audio: ESP32 only (C3 lacks Bluetooth Classic)
- PSRAM-dependent caches: opt-in based on board config
- RTC: external RTC chip on some boards, on-chip on others

Capability flags exposed to Lua via `system.platform()` so plugins can conditionally enable features.

### Userland portability

A plugin written against the kernel ABI runs unchanged across all supported boards, the same way a Linux binary runs on x86 and ARM. The HAL abstraction is the multi-architecture story.

---

## 8. Roadmap

Rough sequencing. Effort estimates assume focused work; calendar time will be longer.

### Phase 10 — Kernel foundations (~3 weeks)
- Per-plugin Lua memory cap + `onLowMemory`
- Per-plugin storage sandbox + `kv.*` API
- Coroutine-backed `task.*` scheduler (re-enable coroutines internally)
- Crash log + structured logging

### Phase 11 — IPC and lifecycle (~2 weeks)
- `events.*` pub/sub bus
- System events published from C
- Service plugin lifecycle (`onStart`/`onStop`/`onTick`)

### Phase 12 — Async I/O and network (~3 weeks)
- mbedTLS integration
- `http.*` async client
- Connection pooling
- Offline queue helper

### Phase 13 — System UI (~3 weeks)
- `ui.notify`, `ui.confirm`, `ui.alert`
- Status bar compositor
- Damage tracking + partial refresh manager
- Theme tokens

### Phase 14 — Developer experience (~2 weeks)
- SDL2 desktop simulator
- Plugin manifest format finalized
- Settings schema + auto-rendered settings UI
- API reference doc generator (extract from C source comments)

### Phase 15 — Time and scheduling (~1 week)
- RTC abstraction + NTP
- `time.scheduleAt`
- Wake-on-timer

### Phase 16 — Audio (Waveshare/ESP32 only) (~3 weeks)
- BT Classic + A2DP source
- Helix MP3 decoder
- Audio service plugin pattern
- I2S DAC routing as alternative output

### Phase 17 — Stabilization
- API stability audit: tag every Lua function as stable/experimental/deprecated
- Plugin manifest enforcement
- v1.0 ABI freeze

After v1.0, the no-break-userspace contract is in full effect.

---

## 9. What We Are Explicitly Not Building

To prevent scope creep, these are out of scope:

- **Multi-window / windowed UI.** Single full-screen activity is correct for e-ink.
- **POSIX or Unix compatibility.** This is not a Linux clone.
- **Threading exposed to plugins.** Coroutines are sufficient and safer.
- **Filesystem-level permissions beyond per-plugin sandbox.** Capability system + sandbox is enough.
- **Plugin signing / code signing.** Premature; rely on community trust + code review for now.
- **General-purpose graphics primitives (SVG, vector paths).** E-ink doesn't reward complexity.
- **Hot-swappable kernel modules.** Drivers compiled in per build.
- **Multiple users.** Single-user device.
- **Shells / REPLs in the released firmware.** Dev-only, behind a debug build flag.

---

## 10. Open Questions

Things to decide before or during implementation:

1. **KV store backend.** LittleFS (proven, ~30 KB) vs flat append-only log (smaller, custom) vs SQLite (~600 KB, full querying). Lean: LittleFS for KV, optional SQLite as a separate registered module for plugins that opt in.

2. **Capability granularity.** Phase 9.0 ships per-namespace capabilities (e.g., `wifi`). Do we need finer (`wifi.connect` vs `wifi.scan`)? Probably not at v1; revisit if abuse emerges.

3. **Plugin install mechanism.** Currently: drop a `.lua` file on SD. Future: a "plugin store" served from a Git-backed repo? OPDS-style catalog feed? Separate concern; defer until userland is mature.

4. **Translation pack format.** Currently JSON. Consider switching to Lua tables for consistency with manifest format (no separate parser, can include logic for pluralization).

5. **Theme system scope.** Just colors? Or also fonts and spacing? E-ink has limited color, so probably tokens are: `theme.fg`, `theme.bg`, `theme.accent`, `theme.muted`, plus `theme.font` and `theme.spacing`. Don't over-engineer.

6. **Plugin error UX.** Show a friendly screen with "this plugin crashed, view log / disable / restart"? Or just log and silently disable? Probably the former — visibility matters for trust.

7. **Should `display.*` expose framebuffer directly or only high-level draw calls?** Currently mixed. Decision: expose both (fast plugins want pixels), but flag direct-framebuffer access as an experimental capability.

8. **Audio service architecture.** Single global audio service, or one per plugin that requests it? Single global; plugins request playback via API; only one plays at a time.

9. **Network on/off vs WiFi-aware.** Is `wifi.*` correct, or should it become `net.*` since the kernel might support Ethernet on some future board? Lean: rename to `net.*` before v1.0 freeze. Cheap change.

10. **Plugin packaging.** Decided: `.ticl` extension, two-form format (plain Lua source OR ZIP bundle, detected by magic bytes). Plain form ships in v1; bundle form lands when a plugin first needs assets/translations/fonts beyond a single file. Documented in section 5.

---

## 11. Success Criteria

By v1.0, this is a success if:

1. A new plugin author can clone the repo, run the simulator, and see their plugin run within 10 minutes.
2. A plugin written against v1.0 still runs on v1.5 without modification.
3. Three or more community-contributed plugins exist that rely on services we built (events, kv, http, audio).
4. The same plugin binary runs on the X4 and the Waveshare-based DIY hardware unmodified.
5. A bad plugin cannot crash the device — it can only crash itself, with a recoverable error and a log entry.
6. End users can install a plugin by copying a file to the SD card, with no other steps.

---

## 12. References and Inspiration

- **PebbleOS architecture documentation** — small kernel + JS apps, capability model
- **PICO-8 documentation** — fixed Lua API as the entire platform contract
- **Roblox Luau task library** — `task.spawn`, `task.delay`, `task.wait` as the canonical Lua async pattern
- **OpenResty / lua-nginx-module** — coroutine-backed async I/O at scale
- **Linux kernel stable API documentation** — no-break-userspace as engineering culture
- **seL4 / Mach** — capability-based microkernel design
- **Pebble SDK and Playdate SDK** — exemplars of constrained-platform developer experience

---

*This document is the source of truth for kernel-level architectural decisions. When the kernel framing conflicts with ad-hoc decisions in code, this document wins. Update it when intent changes; do not let it drift.*
