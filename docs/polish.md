# Polish & Optimization Backlog

Pre-release optimizations to maximize available RAM and performance for plugin developers and users. Apply these after all core features are complete and tested.

## Lua Module Consolidation

### Merge small modules
- `theme.lua` (47 lines, 2 functions) and `status_bar.lua` (60 lines, 1 function) into `ui.lua`
- Eliminates 2 module tables, 2 `require` cache entries, 2 bytecode metadata sets
- Estimated savings: ~500 bytes per module = ~1KB

### Split large modules
- `fonts.lua` (255 lines, 8 functions) into `fonts_core.lua` (init/cleanup) and `fonts_reader.lua` (fallback/detection)
- Non-reader plugins (home, settings, browser) only load core — saves ~2-3KB for those plugins

### Lazy module loading
- Move `require` calls from module top into functions that use them
- Example: home doesn't need `lib.lang` until `onEnter()` — defer the require
- With shared state, modules stay cached after first load so the cost is one-time
- Estimated savings: varies, ~1-2KB per deferred module

## Table & Function Reduction

### Compute orientation mappings on demand
- `buttons.lua` creates 19 table literals for orientation mappings at load time
- Could compute on first `get_mapping()` call and cache — saves ~1KB initial allocation

### Reuse common patterns
- Several plugins duplicate the same render/loop pattern (clear, draw, refresh)
- A `plugin_base.lua` with common lifecycle could reduce per-plugin closure count
- 61 functions across all libs at ~50 bytes per closure = ~3KB total

### Constants to static
- Theme constants are Lua tables but never change — could be C-side constants
- `display.themeGet(key)` instead of Lua table — eliminates theme table from heap

## SDK Cleanup (Flash savings, minimal RAM impact)

### Strip X3 code paths
- `#ifdef EINK_X4_ONLY` around all X3 LUT tables (~840 bytes flash)
- `#ifdef EINK_X4_ONLY` around X3 code branches (~2KB flash)
- Zero RAM impact but cleaner binary

### Replace std::vector in SDK
- `displayWindow()` uses `std::vector` for temp buffer — replace with malloc/free
- Remove `#include <fstream>` and `#include <vector>` from EInkDisplay.cpp

### C rewrite of SDK (major, optional)
- Rewrite 4 SDK modules (EInkDisplay, InputManager, BatteryMonitor, SDCardManager) in pure C
- Eliminates C++ runtime overhead, .eh_frame (23.5KB flash), constructors, template instantiations
- Estimated: ~3-4KB RAM, ~25KB flash savings
- Big effort (~1 day), low RAM return — do last if at all

## Lua Standard Library Trimming

### Remove math library if unused
- `math` costs ~2-3KB in the Lua state
- Audit all plugins for `math.*` usage — may only need `math.floor`, `math.min`, `math.max`, `math.ceil`
- Could replace with local Lua functions and drop the library

### Custom allocator
- Lua's default allocator uses libc `realloc` which fragments easily
- A pool-based allocator for common sizes (16, 32, 64, 128 bytes) could reduce fragmentation
- Complex but potentially significant for long reading sessions

## Measurement Baseline

Current state (as of Phase 9):
- Free heap at boot: 206KB
- Bare Lua state: 4.5KB
- Standard libs: 7.9KB
- CrossLua API registration: 8.3KB
- Lua state total: 20.7KB
- Home plugin + all modules: ~118KB total (including fonts)
- Free heap with home running: 88KB
- Plugin discovery: 12ms (C-side, no Lua)
- Bytecode cache: automatic .luac on SD

## Guiding Principle

Our job is to make as close to a perfectly optimized core as we can, so users and plugin developers have as much RAM and performance as possible. Every KB we save is a KB they can use.
