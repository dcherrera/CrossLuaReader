# Firmware Home Spec

A firmware-bundled copy of the `home` plugin that ensures the device always has a usable boot UI, regardless of SD card state. The bundled home doubles as the recovery surface: it gains a small set of rescue actions (reload SD, toggle WiFi, view diagnostics, restart) that work even when no SD card is present.

This is foundational reliability work and is scheduled near-term — the device should never be in a state where a missing or corrupted SD card produces a black screen with no way out.

---

## Motivation

The runtime currently loads all plugins, including `home`, from the SD card. That works beautifully when the SD card is healthy and present, but it creates a fragile failure mode:

- SD card removed → black screen on boot
- SD card corrupted → black screen on boot
- A bad edit to `home.lua` (syntax error, runtime error in `onEnter`) → black screen on boot
- Sync interrupted mid-write to home → black screen on boot
- User experimenting with custom plugins, breaks the active set → black screen on boot

For an e-ink device this is *especially* user-hostile: the last-rendered ghost stays on the screen even after power-off. The user perceives the device as bricked.

Every reasonable embedded system has a recovery path that doesn't depend on removable storage:

- Linux: `/sbin/init` baked into initramfs, plus a recovery initrd
- macOS: Recovery Partition
- Android: stock recovery image
- Most e-readers: a recovery menu accessible without OS files

CrossLua Reader needs the same property. The cleanest way to provide it on this architecture is to **bundle a copy of the `home` plugin in firmware** and have the plugin manager fall back to it when the SD copy is missing or broken.

This same bundled home gains a small set of rescue actions, turning what would be a "the device is broken" moment into a "the device is fine, here are tools to recover" moment.

---

## Goals

1. **Always boot to a usable UI.** No matter what state the SD card is in (missing, unmounted, corrupted, half-deleted, full of broken plugins), the device boots to a working `home` screen.
2. **Single home plugin source.** The same `home.lua` source file is used for both the SD-loaded version and the firmware-bundled version. No fork, no parallel maintenance.
3. **Mode-aware rendering.** The home plugin detects SD state at runtime and shows different UI accordingly: normal book-library UI when SD is healthy, rescue actions when not.
4. **Rescue features that work without SD.** Reload SD, toggle WiFi, view diagnostics, restart device — all functional without any SD-side dependencies.
5. **Trivial fallback algorithm.** The plugin manager's logic stays simple: try SD first, fall back to embedded bytecode buffer on failure.
6. **No build complexity creep.** The build pipeline gains one well-bounded step: compile and embed `home.lua` as a C array. No new toolchain dependencies in v1.
7. **Stays within the project's architecture.** The pattern of "firmware-bundled fallback for runtime-critical plugins" is opt-in per plugin, doesn't disturb the rest of the plugin model, and matches the project's "thin C runtime + Lua plugins" philosophy.

## Non-goals

- **Bundling all plugins in firmware.** Only `home` qualifies for v1. The criterion is "without this plugin, the device cannot present a usable state to the user." File browsers, readers, settings, etc. are useless without SD content anyway, so they don't qualify.
- **Hot reload of the bundled home.** If the firmware-bundled home is changed (via a firmware update), the device picks it up on next boot. No live-reload of bundled bytecode.
- **A separate "recovery mode" plugin.** Going with one mode-aware home plugin instead of two parallel implementations. See [Architecture decision](#architecture-decision-one-plugin-two-modes) for reasoning.
- **OTA firmware update from rescue mode.** Useful future feature but out of scope for v1. The rescue UI provides the *visibility* (firmware version, free flash) needed for the user to manually update.
- **Plugin signing or trust verification.** A natural future extension once bundled-in-firmware sets the precedent for "trusted code," but not part of v1.

---

## Architecture decision: one plugin, two modes

Two design options exist:

**Option A — Single mode-aware `home.lua`** (chosen)
The same source file ships on SD and bundled in firmware. At runtime, the plugin checks SD state via `storage.is_mounted()` and renders different UI based on the result. SD-present mode shows the normal library view; SD-absent mode shows the rescue UI.

**Option B — Two separate plugins** (rejected)
A rich `home.lua` on SD with the full library UI, and a focused `firmware_home.lua` bundled in firmware with rescue-only features. The plugin manager picks based on SD state.

Option A is preferred because:

- **One source of truth.** No risk of features drifting between the SD home and the firmware home.
- **Rescue features are useful in normal mode too.** "Reload SD" matters when the user replaces the card. "Toggle WiFi" is useful regardless of SD state. About screen with version info is useful for bug reports either way.
- **Simpler plugin manager.** One entry point, one fallback path.
- **Smaller maintenance surface.** Adding a feature to home means changing one file, not two.

The cost of Option A is a slightly larger `home.lua` (a few hundred extra lines for the rescue UI branch). On 16 MB flash, that's invisible. The clarity benefit is worth it.

---

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│ C runtime (firmware)                                             │
│                                                                  │
│  ┌──────────────────────────────┐                                │
│  │ Plugin manager               │                                │
│  │  load_home() {               │                                │
│  │    try SD path               │                                │
│  │    if fail: load embedded    │ ◀──┐                           │
│  │    if both fail: panic       │    │                           │
│  │  }                           │    │                           │
│  └──────────────────────────────┘    │                           │
│                                      │                           │
│  ┌──────────────────────────────┐    │                           │
│  │ Embedded home bytecode       │ ◀──┘ const uint8_t home_luac[] │
│  │  (.rodata, ~5-15 KB)         │      generated at build time   │
│  └──────────────────────────────┘                                │
└──────────────────────────────────────────────────────────────────┘
                          ▲
                          │ Lua execution
                          │
┌──────────────────────────────────────────────────────────────────┐
│ home.lua (single source, runs from either origin)                │
│                                                                  │
│  onEnter():                                                      │
│    if storage.is_mounted():                                      │
│      render_normal_mode()      ◀── library, recent, books        │
│    else:                                                         │
│      render_rescue_mode()      ◀── 4 buttons, no scrolling       │
│                                                                  │
│  Rescue actions:                                                 │
│    Reload SD     → storage.remount(), reload plugins             │
│    Toggle WiFi   → wifi.enable() / wifi.disable()                │
│    About         → version, git hash, heap, flash, battery       │
│    Restart       → system.restart()                              │
└──────────────────────────────────────────────────────────────────┘
```

The plugin manager gains a single new behavior: when loading the home plugin, attempt the SD path first; on failure, load the embedded bytecode buffer. Everything else about plugin loading is unchanged.

---

## The home plugin: dual-mode design

A single `home.lua` file with branching at `onEnter`:

```lua
local lang = require("lib.lang")

local M = {}
local mode = nil  -- "normal" or "rescue"

function M.onEnter()
    if storage.is_mounted() then
        mode = "normal"
        normal_mode_init()
    else
        mode = "rescue"
        rescue_mode_init()
    end
    M.render()
end

function M.loop()
    input.poll()
    if mode == "normal" then
        normal_mode_handle_input()
    else
        rescue_mode_handle_input()
    end
end

function M.onExit()
    -- mode-specific cleanup
end

function M.render()
    if mode == "normal" then
        render_normal()
    else
        render_rescue()
    end
end

return M
```

### Normal mode

The existing home UI: book library, recent reading, library actions. No changes from current behavior. This is the path that runs ~99% of the time.

### Rescue mode

Triggered when `storage.is_mounted()` returns false. Displays a centered 4-button menu, no scrolling, operable with physical buttons:

```
┌────────────────────────────────────────────────────────────┐
│                                                            │
│            CrossLua Reader — Recovery                      │
│            ────────────────────────────                    │
│                                                            │
│            SD card not detected.                           │
│                                                            │
│    ┌─────────────────────────────────────────────┐         │
│    │  [↑]  Reload SD                             │         │
│    │  [→]  Toggle WiFi  (currently: off)         │         │
│    │  [↓]  About / Diagnostics                   │         │
│    │  [←]  Restart Device                        │         │
│    └─────────────────────────────────────────────┘         │
│                                                            │
│    Insert SD card and press Reload SD to continue.         │
│                                                            │
└────────────────────────────────────────────────────────────┘
```

Each button maps to a physical button. No touch, no scrolling. The four buttons are mapped to the four front buttons (Up/Down/Left/Right) using existing `MappedInputManager` semantics.

#### Rescue actions

**Reload SD**
- Calls `storage.remount()`.
- On success: re-discovers plugins, transitions to normal mode (re-renders home with the library UI).
- On failure: shows an inline message ("SD card not readable") and stays in rescue mode.

**Toggle WiFi**
- Reads `wifi.is_connected()` to determine current state.
- Calls `wifi.enable()` or `wifi.disable()`.
- Updates the button label live ("currently: on" / "currently: off").
- Useful for: starting sync after recovery, manually disabling WiFi to save battery, OTA prep.

**About / Diagnostics**
- Renders a sub-screen showing:
  - Firmware version (`system.firmware_version()`)
  - Git commit hash (`system.git_commit()`)
  - Build date (`system.build_date()`)
  - Free heap (`system.free_heap()`)
  - Free flash (`system.free_flash()`)
  - Battery percentage (`system.battery_percent()`)
  - SD card status ("not detected" / "detected, mount failed" / "mounted")
  - WiFi status ("disabled" / "connecting" / "connected: SSID")
- "Back" button to return to rescue menu.
- Critical for the "user reports a bug" support flow — the user can read the version and hash off the screen.

**Restart Device**
- Calls `system.restart()` after a brief confirmation.
- A held-press required (e.g., 1.5s hold) to prevent accidental restart.

---

## Plugin manager fallback behavior

The plugin manager gains a single new code path:

```c
// pseudocode
int load_plugin_home(lua_State *L) {
    // Try SD path
    if (storage_is_mounted()) {
        int r = load_plugin_from_sd(L, "/plugins/home/init.lua");
        if (r == LUA_OK) return r;
        log_warn("SD home failed to load: %s", lua_tostring(L, -1));
        lua_pop(L, 1);  // discard error
    }

    // Fall back to embedded bytecode
    extern const unsigned char home_luac[];
    extern const unsigned int home_luac_len;
    int r = luaL_loadbuffer(L, (const char*)home_luac, home_luac_len, "=home_embedded");
    if (r != LUA_OK) {
        // This should never happen in practice — bytecode was validated at build time.
        log_error("FATAL: embedded home failed to load: %s", lua_tostring(L, -1));
        return r;
    }

    log_info("Loaded home from firmware fallback (SD unavailable or broken)");
    return r;
}
```

Behavior summary:

- SD present + valid `/plugins/home/`: loads from SD, no change from current behavior.
- SD present + broken `/plugins/home/` (syntax error, missing files): loads from firmware fallback, logs the failure.
- SD absent: loads from firmware fallback.
- Firmware fallback also broken: panic. This is a build-failure-class bug; should never happen because the build pipeline validates the embedded bytecode.

The fallback is one-way per boot. If the firmware home is loaded and the user later inserts an SD card, hitting "Reload SD" remounts and triggers a *replacement* of the active plugin with the SD copy (assuming it loads cleanly).

---

## Build pipeline

A new build step compiles `home.lua` to bytecode and embeds it in firmware as a C array.

### v1: source bundling (simpler, ship this first)

For the initial implementation, **bundle source as a string** rather than bytecode. Rationale:

- Avoids the cross-luac toolchain dance.
- Source for `home.lua` is small (probably ~5-15 KB) — flash impact is negligible.
- Parse cost at boot is ~50-200 ms — invisible against the multi-second e-ink boot anyway.
- Keeps the build pipeline trivially understandable for contributors.

Build step:

```sh
# Generate src/embedded/home_lua.h from sdcard/plugins/home/init.lua
xxd -i sdcard/plugins/home/init.lua > src/embedded/home_lua.h

# Or, equivalently, a small Python script that wraps the bytes in a clean
# `const unsigned char home_lua[]` declaration with a length constant.
```

The PlatformIO `extra_scripts` directive runs this before compilation. The C code includes `home_lua.h` and uses `luaL_loadbuffer` exactly as it does for SD-loaded plugins.

### v2: bytecode bundling (after cross-luac is sorted)

Migrate to true bytecode bundling once a target-matching `luac` is available in the build pipeline:

- Add a cross-luac built with the same `lconfig.h` as the target firmware (matching `LUA_INT_TYPE`, `LUA_FLOAT_TYPE`, endianness).
- `luac -o home.luac sdcard/plugins/home/init.lua`
- `xxd -i home.luac > src/embedded/home_luac.h`
- Plugin manager uses `luaL_loadbuffer` on the bytecode buffer; bytecode header validates compatibility.

Benefits:
- Smaller (bytecode ~3-8 KB vs source ~5-15 KB)
- No parse cost at boot
- Matches the runtime currency the SD path already uses (per existing bytecode caching)

This is a phase-2 optimization. Don't gate v1 on it.

---

## Required new runtime APIs

Small, focused additions. Most are general-purpose enough that they'll get reused by other plugins.

### `storage.*` additions

```
storage.is_mounted() → bool
    True if an SD card is mounted and the filesystem is accessible.

storage.remount() → ok|err, errmsg
    Attempt to detach the current SD mount and remount fresh.
    Returns true on success, or (false, errmsg) on failure.
    Triggers re-discovery in the plugin manager on success.

storage.get_state() → "missing" | "detected_mount_failed" | "mounted"
    Detailed state for the About screen.
```

### `system.*` additions

```
system.firmware_version() → string
    e.g. "1.0.3"

system.git_commit() → string
    Short hash, e.g. "b4e7a9f"

system.build_date() → string
    ISO date, e.g. "2026-04-27"

system.free_heap() → int
    Bytes of free heap (already exists, ensure exposed)

system.free_flash() → int
    Bytes free in the firmware partition (or app data partition)

system.battery_percent() → int
    0-100, or -1 if unavailable

system.restart()
    Soft reboot. Does not return.
```

### `wifi.*` additions (verify if missing)

```
wifi.is_enabled() → bool
wifi.is_connected() → bool
wifi.get_ssid() → string|nil
wifi.enable() / wifi.disable()
```

These probably exist in some form per existing build_spec. Verify and round out if any are missing.

---

## Rescue UI design constraints

A few design rules that the rescue UI must follow, because they're load-bearing for actually being useful in a "device is broken" moment:

1. **Operable with physical buttons only.** No touch (the X4 has none anyway), no on-screen scrolling. Four actions max, mapped to the four front buttons.
2. **No SD dependencies.** Cannot read fonts from SD (uses the boot font baked into firmware). Cannot read translations from SD (English-only in v1, with per-string defaults in `home.lua`). Cannot read settings from SD (uses NVS-stored defaults if any).
3. **No WiFi dependencies.** Must render and operate even when WiFi is off and disabled.
4. **Bounded memory.** Stays within the standard plugin RAM budget (~89 KB available per the architecture doc). Should be well below since it's a simple menu.
5. **Self-explanatory.** No reliance on previous user knowledge. The user might be a non-technical owner whose SD card died.
6. **Recoverable from misclicks.** Restart action requires hold-confirm. About sub-screen has a clear "Back" button.
7. **Visible firmware version.** No matter where the user gets stuck, they can read the version and git hash from the About screen for support purposes.

---

## Memory and flash budget

| Resource | Cost | Notes |
|----------|------|-------|
| Embedded `home.lua` source (v1) | ~5-15 KB flash | One copy in `.rodata` |
| Embedded `home.luac` bytecode (v2) | ~3-8 KB flash | Replaces source after migration |
| Plugin manager additions | <1 KB code | Small fallback function + path |
| New runtime APIs (storage/system extensions) | ~2 KB code | Mostly thin wrappers around existing functions |
| Rescue UI runtime RAM | ~5 KB | Four buttons + state, well under plugin ceiling |
| **Total flash impact** | **~10-20 KB** | Trivial against 16 MB |
| **Total RAM impact** | **~5 KB during rescue mode** | Stays under plugin budget |

---

## Phased rollout

### Phase 1 — Ship the reliability guarantee (target this)

- Build pipeline adds source-bundling step for `home.lua` (defer bytecode to phase 2).
- Plugin manager fallback path: try SD, fall back to embedded source.
- `home.lua` gains mode detection via `storage.is_mounted()`.
- Rescue UI implemented: Reload SD, Toggle WiFi, About, Restart.
- New runtime APIs: `storage.is_mounted`, `storage.remount`, `system.firmware_version`, `system.git_commit`, `system.build_date`, `system.free_heap`, `system.free_flash`, `system.battery_percent`, `system.restart`.
- Tested: pull SD card mid-boot → rescue UI appears, hit Reload SD with card reinserted → transitions to normal mode.

### Phase 2 — Polish and migrate to bytecode

- Cross-luac added to build pipeline.
- Migrate from source bundling to bytecode bundling.
- About screen gets richer diagnostics (uptime, last crash reason if available, mDNS status, etc.).
- Factory Reset action (held-confirm) wipes NVS to defaults.
- Firmware version display gains a "check for updates" link if OTA infrastructure lands.

### Phase 3 — Speculative

- USB-mode emergency recovery (mount SD as USB Mass Storage when in rescue mode, so user can repair files from a connected computer).
- Bundled-in-firmware error_recovery plugin for crash-screen fallback (most of this is already C-side per existing crash recovery code).
- Pattern extension: bundle additional system-level plugins (e.g. a minimal settings) if the use case justifies it.

---

## Open questions

- **Where exactly does the build script generate `home_lua.h`?** Tentatively `src/embedded/home_lua.h`, gitignored. Codegen runs every PlatformIO build. The pre-script is straightforward (a few lines of Python or bash).
- **What font does rescue mode use?** It must not depend on SD-loaded `.cfont` files. The existing boot font (per `e1b2d36` commit "boot font, crash screen") provides this. Confirm the boot font supports all glyphs the rescue UI needs.
- **What language does rescue mode use?** English only in v1. The `i18n` system loads from SD, which won't be available. Each string in the rescue UI hard-codes English. Phase 2 can add a small embedded translation table if internationalization matters here.
- **What if WiFi credentials are stored only on SD?** Then "Toggle WiFi" can enable the radio but has no SSID/password to connect to. UX should reflect this: "WiFi enabled but no network configured (insert SD or use rescue WiFi config)." Phase 2 might add a simple SSID/password input in rescue mode using the existing `KeyboardEntryActivity` pattern.
- **How does the rescue mode interact with sleep/power management?** Should it keep the device awake (so the user can act on it), or use normal sleep timers? Tentative answer: extend the sleep timeout in rescue mode (e.g., 5 minutes vs 60 seconds), since the user is likely actively interacting.
- **Should the user be able to disable the firmware fallback?** Some advanced users might want "fail loudly if SD home is broken" behavior. Tentative answer: no, not in v1. The fallback is a safety net, not a configuration choice.
- **What happens if firmware-bundled bytecode fails to load?** This indicates a corrupted firmware install or build-pipeline bug. Plugin manager logs to serial and renders a hard-coded C-side panic screen ("Firmware corrupted — please reflash") using the boot font. This path should never trigger in shipping firmware but must exist defensively.

---

## Why this is high priority

Three reasons this slots ahead of most other roadmap items:

1. **It removes a class of black-screen failures.** Right now any SD-side regression — bad commit, sync glitch, file corruption, removed card — produces a non-recovering device. That's a quality bar that needs to be cleared before serious user adoption.
2. **It enables future features that depend on rescue access.** OTA, plugin signing, factory reset, USB recovery all want a guaranteed-bootable surface to operate from.
3. **It's a small implementation.** Realistically a few days of focused work for v1 (source bundling, fallback path, rescue UI). The architectural reasoning is settled, the runtime additions are small, the home plugin gains a focused new branch. Good ROI for the time invested.

This belongs at the root of the repo (alongside `build_spec.md` and `build_plan.md`) rather than under `Specs/` because it's near-term active work, not a future-aspirational design.
