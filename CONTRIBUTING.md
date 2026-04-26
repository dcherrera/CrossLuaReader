# Contributing to CrossLua Reader

Thanks for your interest. CrossLua Reader is intentionally designed so that *most* contributions don't require touching the firmware. There are three paths in, ordered from easiest to heaviest:

1. [**Lua plugins**](#lua-plugins) — write a `.lua` file, drop it on the SD card, send a PR. No toolchain, no reflash.

2. [**Translations**](#translations) — copy `sdcard/languages/en/lang.json`, translate the strings, PR. No code at all.

3. [**Core firmware**](#core-firmware) — C runtime changes, new HAL APIs, new Lua bindings. **Currently closed to external PRs.** See the section below for what that means and when it changes.

Most contributors should be on path 1 or 2. The Lua plugin and translation paths are wide open and actively encouraged.

---

## Lua plugins

This is the path the project is built around. The C runtime is intentionally minimal so that all application logic — readers, menus, network features, utilities — can live as Lua scripts on the SD card.

### Quick start

1. Read [`docs/plugin-guide.md`](docs/plugin-guide.md) for the lifecycle (`onEnter`, `loop`, `onExit`) and [`docs/lua-api.md`](docs/lua-api.md) for the available APIs.

2. Create your plugin folder at `community_plugins/<plugin_name>/` and put your main script inside as `<plugin_name>.lua`. The per-plugin folder lets you bundle helper modules, plugin-specific translations, icons, or any other assets alongside the main script.

3. Copy the folder onto your SD card under `/plugins/` and reload the device. The plugin manager auto-discovers new plugins.

4. Iterate until it works.

5. Open a PR adding your `community_plugins/<plugin_name>/` folder plus a short description of what the plugin does.

### Standards

Plugins ship to real users on a 380KB-RAM device. Please:

- **No busy loops.** If your `loop()` runs every frame, yield with `coroutine.yield()` or use `input.waitButton()` for blocking waits. A plugin that pegs the CPU drains the battery.

- **No infinite allocations.** Pre-allocate tables outside the loop where possible; avoid creating large strings inside `loop()`.

- **Free what you allocate.** Close files in `onExit()`, release coroutines, clear references to large tables.

- **Use the i18n module** (`require("lib.lang")`) for any user-facing string. Don't hardcode English text — see the [Translations](#translations) section.

- **Follow the existing style.** Snake_case for variables and functions, two-space indent, descriptive names. See [`coding-best-practices.md`](coding-best-practices.md).

### Requirements for acceptance

- MIT-licensed (or compatible permissive license).

- No network calls to non-public APIs (no scraping behind login walls, no API keys baked into source).

- No `os.execute`, no shelling out, no attempts to escape the Lua sandbox.

- Passes a basic code audit — readable, no obvious memory leaks, no surprising behavior.

- Tested on real X4 hardware (not just in your editor).

Accepted plugins land in the `community/` directory of the next release.

---

## Translations

CrossLua Reader can be translated into any language by editing one JSON file. No code, no toolchain — if you can edit a text file, you can ship a language pack.

### Quick start

1. Copy `sdcard/languages/en/lang.json` → `sdcard/languages/<your-code>/lang.json`. Use an [ISO 639-1 code](https://en.wikipedia.org/wiki/List_of_ISO_639-1_codes) (`es`, `fr`, `ja`, `ar`, etc.).

2. Translate the values in the `strings` object. **Leave the keys untouched** — those are stable identifiers used by the code.

3. Update the top-level fields:

   - `code` — match your folder name.

   - `name` — your language's name *in its own script* (`Español`, `日本語`, `العربية`). This is what appears in the language picker.

   - `direction` — `"ltr"` or `"rtl"`. The runtime handles bidirectional text rendering automatically.

   - `fontFamily` — `null` if your language uses Latin or Cyrillic glyphs already shipped. Otherwise, the family name of a font you bundle (see below).

4. Drop the folder onto your SD card, restart, switch language in Settings, verify it renders.

5. Open a PR adding the `languages/<code>/` folder.

### Custom fonts for non-Latin scripts

If your language needs glyphs not in the default fonts (Hebrew, Arabic, CJK, Greek, Devanagari, etc.), bundle the font alongside the language pack:

```
sdcard/languages/<code>/
├── lang.json           (with fontFamily: "YourFamilyName")
└── fonts/
    └── YourFamilyName-14-Regular.cfont
```

Convert `.ttf`/`.otf` to `.cfont` using the converter in `tools/`. Format details in [`docs/cfont-format.md`](docs/cfont-format.md).

### Partial translations are welcome

Any keys you skip fall back to English at runtime. Ship what you have — others can fill in the rest later. A 60% translation is far more useful than no translation.

### Review

Translation PRs land fast. The review is:

- UTF-8 encoding sanity check

- Render check on real hardware

- That keys are unmodified

---

## Core firmware

**Status: closed to external PRs during the foundational phase.**

Changes to the C runtime, HAL drivers, font renderer, and Lua API bindings live here. While the project is still being shaped, I'm keeping core firmware closed to outside contributions. This isn't a comment on anyone's skill — it's a deliberate choice to keep the architecture coherent until the foundation is settled. Once that's done, this section will be updated to open things up. Until then, please assume the answer is no.

If you'd still like to send a PR despite the above, read carefully:

- **Large PRs will be ignored outright.** A "I refactored your renderer" or "I rewrote the plugin manager" PR is dead on arrival regardless of quality. Don't waste your time.

- **Small, pointed PRs *may* be considered.** The realistic happy path looks like: *"I built a plugin. It needs one specific function added to the firmware to do its thing. Here it is — a few lines of C, follows my conventions, includes a test plugin that uses it."* That kind of PR has a chance.

- **Don't get your hopes up.** Even small, well-written PRs may be rejected for architectural reasons that aren't visible from outside. I'll look at them, but acceptance is not the default.

- **Code must follow **[**`coding-best-practices.md`**](coding-best-practices.md) exactly. Stack safety, heap discipline, flash placement, naming, logging, error handling — all of it. PRs that skip the standards will be closed without lengthy discussion.

### If you think you need a firmware change

Before writing any C, **open a GitHub issue** describing what you're trying to build. Most "the firmware needs to do X" requests can actually be solved in pure Lua — and an issue gets you a quick "here's how to do that as a plugin" or "noted for the roadmap" without you spending time on code that won't land. **Don't start coding without prior agreement.** Without an issue conversation up front, even good work is likely to go nowhere.

### Standards (for the eventual happy path)

When core firmware does open up, the bar is the C runtime targets a 380KB RAM ceiling on a single-core RISC-V at 160MHz. Highlights from [`coding-best-practices.md`](coding-best-practices.md):

- **Stack safety**: local variables under 256 bytes; static or heap allocation for larger buffers.

- **Heap discipline**: justify every `malloc`. Free deterministically. No heap allocations on the hot path.

- **Flash placement**: large constants must be `static const` or `constexpr`.

- **No exceptions, no RTTI** (compile flags exclude both).

- **Use the HAL**, not the SDK directly — `hal_display`, `hal_storage`, `hal_gpio`, `hal_power`.

- **Logging**: `LOG_INF`, `LOG_DBG`, `LOG_ERR`. Never raw `Serial.print`.

See [`docs/architecture.md`](docs/architecture.md) for the high-level structure and [`build_spec.md`](build_spec.md) for the full Lua API surface.

### Verification (for the eventual happy path)

When core firmware contributions open, this will be the minimum bar before submission:

- \[ \] `pio run` succeeds with zero warnings.

- \[ \] `pio check` passes.

- \[ \] `find lib src -name "*.c" -o -name "*.h" | xargs clang-format -i` produces no diff.

- \[ \] Tested on real X4 hardware (not just compiled).

- \[ \] Free heap after your change is within 5KB of baseline. Log it with `system.heapFree()` and include the number in your PR.

- \[ \] No new RAM allocations on the hot path (rendering, input handling).

- \[ \] If you added a new Lua API: documentation in `docs/lua-api.md`, an example, and a test plugin demonstrating it.

---

## AI assistance

AI tools (Copilot, Claude, ChatGPT, Cursor, etc.) are welcome here. Use them freely — especially for plugins and translations, where the bar is *"does it work and is it useful"*, not *"did a human handcraft every line."* If AI helped you write something good, that's still good.

What I do ask:

**Disclose it in your PR description.** Not as a confession — just so I know what kind of review the code needs. A single line is enough:

- *"Drafted with Claude, reviewed and tested by me."*
- *"Tab-completion via Copilot."*
- *"Hand-written; AI was only used for the translation strings."*
- *"Generated by ChatGPT, then heavily edited."*

Level of detail isn't important — knowing AI was in the loop is. AI-assisted PRs are reviewed exactly the same as any other; the disclosure just helps me know where to look more carefully (hallucinated APIs, invented function signatures, over-engineered abstractions).

**You're responsible for what you submit.** *"The AI wrote it"* is not a defense if the code doesn't work, doesn't compile, calls APIs that don't exist, or violates the project's standards. Read what you're submitting. Test it on real hardware. Be ready to explain why your code does what it does.

**Common AI failure modes to watch for** before you submit:

- **Hallucinated APIs.** AI tools regularly invent functions like `display.fadeIn()` or `storage.exists()` that don't exist in this project. Cross-check every API call against [`docs/lua-api.md`](docs/lua-api.md).

- **Bloat.** AI tends to add abstractions, defensive error handling, and comments you don't need. The project's style is direct — strip what isn't load-bearing.

- **Hardcoded English strings.** AI defaults to writing English strings inline. Use `tr("key")` from `lib.lang` and add the keys to `sdcard/languages/en/lang.json`.

- **Mixed conventions.** AI samples patterns from thousands of projects at once. Match the existing style in *this* repo, not generic Lua you found in a tutorial.

- **Phantom dependencies.** AI may `require` modules that don't exist or assume libraries you haven't shipped. Run the plugin on real hardware before submitting.

**Claude Code is preconfigured.** I've intentionally left my Claude Code setup in the repo — the `.claude/` directory, the `.skills/` files, and the `CLAUDE.md` symlink. They're not stragglers from my development environment, they're there for you. If you use Claude Code (or any AI tool that reads `CLAUDE.md`), the assistant will already know my coding standards, style preferences, project structure, and the gotchas to watch for. That means you can skip a lot of the *"explain this project to the AI"* prompting and get straight to building. This is especially aimed at people who want to contribute but aren't technical enough to brief an AI on a project's conventions from scratch — open the repo in Claude Code, describe what you want to make, and the assistant should already have most of what it needs to produce something I'd actually accept. Other AI tools (Cursor, ChatGPT with file uploads, etc.) can use the same files manually — point them at `CLAUDE.md` and `coding-best-practices.md` at the start of your session.

If you didn't use AI, you don't need to say anything special — that's the assumed baseline.

---

## PR process

### Branch naming

```
feature/<short-description>       New features
fix/<issue-number>-<description>  Bug fixes
refactor/<component>              Code refactoring
docs/<topic>                      Documentation only
plugin/<plugin-name>              New community plugin
lang/<code>                       New language pack
```

### Commit messages

Follow conventional commits style:

```
<type>: <short summary, ≤50 chars>

<optional details — explain *why*, not what>
```

Types: `feat`, `fix`, `refactor`, `docs`, `test`, `chore`, `perf`, `plugin`, `lang`.

Examples from the actual git history:

- `feat: Phase 6 — settings persistence, physical button bar, content area`

- `fix: orientation-aware button remapping and responsive input`

- `docs: update input API and plugin guide for button remapping`

### Description requirements

In the PR description:

1. **What** the change does, in plain English.

2. **Why** — link the issue, describe the user-facing problem, or explain the new capability.

3. **Testing** — what you tested on, what you observed. "Tested on X4 in all four orientations" is a great answer; "compiled" is not.

4. **Screenshots or photos** if it's UI-visible (eink screens photograph fine on a phone).

### Review timeline

This is a hobby project. Expect:

- **Translations**: 1-3 days (just a sanity check).

- **Plugins**: 1-2 weeks (depends on plugin size).

- **Core firmware**: not currently being merged from external contributors (see the [Core firmware](#core-firmware) section). Small, pointed PRs that fit the criteria there will get a look but no timeline.

If a PR is sitting for longer than expected, ping the issue. Sometimes things slip.

---

## Reporting bugs

Open a GitHub issue with:

- **Device**: X4 model, hardware revision if you know it.

- **Firmware version**: from the About screen, or git commit hash.

- **Repro steps**: what you did, what you expected, what happened instead.

- **SD card layout**: list of plugins/fonts/languages installed (helps narrow scope).

- **Serial logs** if you can capture them (`pio device monitor`).

- **A photo of the screen** if it's a rendering issue.

Don't include personal data — book filenames are fine, book contents are not. Don't paste credentials, WiFi passwords, etc.

---

## Code of conduct

Be kind. Assume good intent. Don't gatekeep — someone learning Lua for the first time is a *great* contributor profile, not a problem to manage. Disagreement is fine; condescension is not.

---

## Questions

For anything that doesn't fit a bug report or a PR — design questions, "how do I do X," "is this welcome" — open a GitHub Discussion or ping the project Discord (link in the README when available). Don't open an issue for questions; issues are for actionable bugs and feature requests.

---

CrossLua Reader is a [TeamIDE](https://teamide.dev) project. If you'd like to support development, see [Support the project](README.md#support-the-project).
