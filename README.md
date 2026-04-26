# CrossLua Reader

A Lua-powered firmware runtime for the **Xteink X4** e-paper reader.

**A [TeamIDE](https://teamide.dev) project.**

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

The goal is to let people customize and extend the firmware without ever touching a C++ compiler. If you build a plugin you think others would enjoy, send a PR — I'll audit it and ship it in the community plugins directory.

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

## Contributing

CrossLua Reader is designed so contributing is as simple as writing a Lua script.

- **Try an idea** — write a `.lua` file, drop it on your SD card, reload. No toolchain, no flash.
- **Share it** — open a PR with your plugin and a short description.
- **Get it shipped** — accepted plugins land in the `community/` directory of the next release.

Plugins must be MIT-licensed, free of network calls to non-public APIs, and pass a basic code audit (no `os.execute` shenanigans, no busy loops that wreck the battery).

**Using AI to contribute is welcome** — please mention AI use in your PR description. I've intentionally left my Claude Code setup (`.claude/`, `.skills/`, `CLAUDE.md`) in the repo so contributors using Claude Code (or any tool that reads `CLAUDE.md`) start with my conventions, style preferences, and project gotchas already loaded. That's especially aimed at folks who want to contribute but aren't deeply technical — open the repo in Claude Code, describe what you want to build, and the assistant should know enough to make something close to what I'd accept. Full AI policy and contribution details in [CONTRIBUTING.md](CONTRIBUTING.md).

## Translations

CrossLua Reader can be translated into any language by dropping a single JSON file on the SD card. No code, no toolchain — if you can edit a text file, you can ship a language pack.

Each pack lives at `sdcard/languages/{code}/lang.json` (where `{code}` is the ISO 639-1 code: `es`, `fr`, `ja`, `ar`, etc.) and follows the same schema as `sdcard/languages/en/lang.json`:

```json
{
  "code": "es",
  "name": "Español",
  "direction": "ltr",
  "fontFamily": null,
  "strings": {
    "home": "Inicio",
    "settings": "Configuración"
  }
}
```

A few notes for translators:

- **`name`** should be the language's name *in its own script* (`Español`, `日本語`, `العربية`) — that's what shows up in the language picker.
- **`direction`** is `"ltr"` or `"rtl"`. The runtime handles bidirectional text rendering automatically.
- **`fontFamily`** is `null` if your language uses Latin or Cyrillic glyphs already shipped with the firmware. For scripts that aren't covered (Hebrew, Arabic, CJK, Greek, etc.), bundle a `.cfont` file inside `languages/{code}/fonts/` and set `fontFamily` to its family name. The `.cfont` format is documented in `docs/cfont-format.md` and there's a converter in `tools/`.
- **Partial translations are welcome.** Any keys you skip fall back to English at runtime, so you can ship what you have and the community can fill in the rest.

To contribute a language pack, open a PR adding your `languages/{code}/` folder. Translation PRs land fast — there's no code to audit, just a quick sanity check on UTF-8 encoding and that the strings render in the app.

## Support the project

I've been writing code since high school — starting with LAMP-stack stuff back when that was the move. Built a little browser-based OS, a custom CMS, a static site generator. But coding has always been a side thing, never my career: I spent two decades in the electronics recycling industry, where I built my own inventory system to keep the doors open. So I'm a developer in practice, more hobbyist than professional, and I've been at it long enough to know what I'm doing.

CrossLua Reader is one of several projects I'm building under [TeamIDE](https://teamide.dev). I'm not great at self-promotion or monetization, so if this is useful to you, send me some love at [teamide.dev/support](https://teamide.dev/support). Every bit helps me keep TeamIDE's projects going.

## License

MIT
