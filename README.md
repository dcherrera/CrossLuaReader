# CrossLua Reader

A Lua-powered firmware runtime for the **Xteink X4** e-paper reader.

CrossLua Reader is a C runtime that turns the Xteink X4 into an extensible e-reader platform. The firmware provides hardware drivers, a font renderer, and a Lua 5.4 interpreter. Everything else — readers, menus, network features — runs as Lua plugins loaded from the SD card.

Built on the hardware knowledge from [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), rewritten from the ground up in pure C.

## Why

CrossPoint Reader is an excellent e-reader firmware, but it's a monolithic C++ application. Every feature is compiled into the firmware. Adding new functionality requires C++ embedded development, a PlatformIO toolchain, and a firmware flash.

CrossLua Reader takes a different approach: the firmware is a thin ~500KB runtime, and all application logic lives as Lua scripts on the SD card. No recompilation, no reflashing — just drop a `.lua` file on the SD card:

- Want to browse Project Gutenberg? Drop `gutenberg.lua` on the SD card.
- Want to read JSON files? Drop `json_viewer.lua` on the SD card.
- Want an RSS feed reader? Drop `rss_reader.lua` on the SD card.
- Want a dictionary lookup? Drop `dictionary.lua` on the SD card.
- Want a note-taking tool? Drop `notes.lua` on the SD card.
- Want a daily quote reader? Drop `daily_quotes.lua` on the SD card.
- Want a Bible reader with chapter navigation? Drop `bible.lua` on the SD card.
- Want a Pomodoro timer for reading sessions? Drop `pomodoro.lua` on the SD card.

## Architecture

```
SD Card                          Flash (~500KB)
├── plugins/                     ├── C HAL drivers
│   ├── epub_reader.lua          ├── Font loader + renderer
│   ├── txt_reader.lua           ├── Framebuffer renderer
│   ├── md_reader.lua            ├── Lua 5.4 interpreter
│   ├── file_browser.lua         ├── C → Lua API bindings
│   ├── settings.lua             └── Plugin manager
│   ├── sefaria.lua
│   └── ...
├── fonts/
│   ├── NotoSans-14-Regular.cfont
│   └── ...
└── books/
```

## Features

- **Plugin system** — extend the device by dropping `.lua` files on the SD card
- **C runtime** — minimal flash footprint, maximum headroom (C++ only in SDK bridge layer)
- **SD-loadable fonts** — add fonts without reflashing (.cfont format)
- **Hebrew/RTL support** — bidirectional text rendering built into the core
- **Full Lua 5.4** — tables, closures, coroutines, string manipulation
- **Native API** — display, input, storage, WiFi, fonts, ZIP, XML, JSON exposed to Lua

## Hardware

- **MCU**: ESP32-C3 (single-core RISC-V @ 160MHz)
- **RAM**: ~380KB (no PSRAM)
- **Flash**: 16MB
- **Display**: 800x480 e-ink
- **Storage**: SD card
- **Device**: Xteink X4

## Status

Early development. See `build_plan.md` for the phased roadmap.

## License

MIT
