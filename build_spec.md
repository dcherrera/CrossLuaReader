# Build Spec: Lua Plugin Runtime for CrossPoint Reader

## Problem

CrossPoint Reader's functionality is entirely baked into the firmware at compile time. Adding new features (file formats, network integrations, UI screens) requires C++ development, a full PlatformIO toolchain, and a firmware flash. This limits the device to what the core developers build and ship.

The firmware currently uses 89% of the 6.5MB flash partition, leaving little room for growth. Font data alone consumes ~4MB, and application logic (EPUB engine, UI activities, network features) takes another ~1MB. Yet most of this code is text parsing, UI flow, and network logic — not hardware-critical work.

## Goal

Build a **pure C runtime + Lua plugin architecture** as a new firmware for the Xteink X4:

- Core firmware (~500KB): C runtime with C++ SDK bridge, display renderer, font loader, Lua 5.4 interpreter
- Everything else: Lua scripts and font files on the SD card
- New features are drag-and-drop `.lua` files — no recompilation, no reflashing

## Language Choice: Pure C Runtime with C++ SDK Bridge

The runtime is written in C. The only C++ is thin `extern "C"` bridge files (~50 lines total) wrapping the open-x4-sdk hardware drivers.

**Why C for the runtime:**
- Lua's API is natively C — no wrappers or binding layers needed
- ESP-IDF is C — direct access without extern/mangling
- Smaller binary — no C++ runtime, RTTI stubs, or exception tables
- The runtime only does hardware access + Lua bindings — no need for C++ abstractions
- Simpler toolchain and faster compilation

**Why keep the SDK in C++:**
- The open-x4-sdk is proven, community-maintained, and rarely changes (~1,738 lines)
- Hardware register access that works shouldn't be rewritten for purity
- PlatformIO handles C/C++ mixed linking natively

## Target Users

- CrossPoint users who want to extend their device (Sefaria browser, new formats, tools)
- Developers who want to build features without a C++ embedded toolchain
- The CrossPoint community — lower barrier to contribution

## Architecture Overview

```
┌─────────────────────────────────────────────┐
│                  SD Card                     │
│                                              │
│  /plugins/                                   │
│    epub_reader.lua     (EPUB parsing/layout) │
│    txt_reader.lua      (TXT reader)          │
│    md_reader.lua       (Markdown reader)     │
│    file_browser.lua    (File browser UI)     │
│    settings.lua        (Settings menus)      │
│    opds_browser.lua    (OPDS catalog)        │
│    kosync.lua          (KOReader sync)       │
│    sefaria.lua         (Sefaria browser)     │
│    wifi_manager.lua    (WiFi connection)     │
│    ota_updater.lua     (Firmware updates)    │
│                                              │
│  /fonts/                                     │
│    NotoSans-14-Regular.cfont                 │
│    NotoSans-14-Bold.cfont                    │
│    Bookerly-14-Regular.cfont                 │
│    Ubuntu-12-Regular.cfont                   │
│    ...                                       │
│                                              │
│  /books/                                     │
│    book.epub, notes.md, torah.txt            │
└──────────────┬──────────────────────────────┘
               │ SPI (SD card bus)
┌──────────────┴──────────────────────────────┐
│           Native C++ Runtime (~500KB)        │
│                                              │
│  ┌─────────────┐  ┌──────────────────────┐  │
│  │ Lua 5.4     │  │ C API Layer          │  │
│  │ Interpreter │  │                      │  │
│  │ (~100KB)    │  │  display.*           │  │
│  │             │◄─┤  input.*             │  │
│  │ Loads and   │  │  storage.*           │  │
│  │ runs .lua   │  │  wifi.*              │  │
│  │ from SD     │  │  font.*              │  │
│  └─────────────┘  │  zip.*               │  │
│                    │  system.*            │  │
│  ┌─────────────┐  └──────────────────────┘  │
│  │ C HAL       │                             │
│  │ Drivers     │  E-ink, SPI, GPIO, WiFi,   │
│  │ (pure C)    │  SD card, Battery, USB      │
│  └─────────────┘                             │
│                                              │
│  ┌─────────────┐                             │
│  │ Font Loader │  Loads .cfont from SD,      │
│  │ & Renderer  │  decompresses on demand,    │
│  │ (pure C)    │  renders glyphs to buffer   │
│  └─────────────┘                             │
│                                              │
│  ┌─────────────┐                             │
│  │ Plugin Mgr  │  Discovers, loads, and      │
│  │ (pure C)    │  manages Lua plugins         │
│  └─────────────┘                             │
└──────────────────────────────────────────────┘
```

## Native API Surface

The C++ runtime exposes these modules to Lua scripts:

### display.*
```lua
display.clear()                          -- Clear framebuffer
display.drawText(fontId, x, y, text)     -- Draw text (handles BiDi internally)
display.drawTextStyled(fontId, x, y, text, style)  -- With bold/italic/underline
display.drawLine(x1, y1, x2, y2)        -- Draw line
display.drawRect(x, y, w, h)            -- Draw rectangle
display.fillRect(x, y, w, h)            -- Fill rectangle
display.drawImage(path, x, y, w, h)     -- Draw BMP/JPG from SD
display.refresh()                        -- Push framebuffer to e-ink (fast)
display.refreshFull()                    -- Full e-ink refresh (no ghosting)
display.width()                          -- Screen width (orientation-aware)
display.height()                         -- Screen height
display.getTextWidth(fontId, text)       -- Measure text width in pixels
display.getLineHeight(fontId)            -- Line height for font
display.getViewableMargins()             -- Returns top, right, bottom, left
```

### input.*
```lua
input.waitButton()                       -- Block until button press, return button ID
input.poll()                             -- Non-blocking check, return button or nil
input.isPressed(button)                  -- Check if button currently held
input.getHeldTime()                      -- How long current button held (ms)
-- Constants: input.BACK, input.CONFIRM, input.LEFT, input.RIGHT,
--            input.UP, input.DOWN, input.PAGE_FORWARD, input.PAGE_BACK
```

### storage.*
```lua
storage.read(path)                       -- Read entire file as string
storage.readBytes(path, offset, length)  -- Read byte range
storage.write(path, content)             -- Write string to file
storage.exists(path)                     -- Check if file/dir exists
storage.mkdir(path)                      -- Create directory
storage.remove(path)                     -- Delete file
storage.list(path)                       -- List directory contents
storage.fileSize(path)                   -- File size in bytes
```

### wifi.*
```lua
wifi.connect(ssid, password)             -- Connect to network
wifi.disconnect()                        -- Disconnect
wifi.isConnected()                       -- Check connection status
wifi.getIP()                             -- Get IP address string
wifi.fetch(url)                          -- HTTP GET, return body as string
wifi.fetchJson(url)                      -- HTTP GET + JSON parse
wifi.post(url, body, contentType)        -- HTTP POST
wifi.download(url, destPath, callback)   -- Download file to SD with progress
```

### font.*
```lua
font.load(path)                          -- Load .cfont from SD, return fontId
font.unload(fontId)                      -- Free font resources
font.list()                              -- List available fonts on SD
-- Built-in font IDs always available as font.UI_10, font.UI_12
```

### zip.*
```lua
zip.open(path)                           -- Open ZIP/EPUB archive, return handle
zip.list(handle)                         -- List entries in archive
zip.read(handle, entryName)              -- Read entry as string
zip.close(handle)                        -- Close archive
```

### xml.*
```lua
xml.parse(text)                          -- Parse XML string into table
xml.find(node, tag)                      -- Find child elements by tag
xml.attr(node, name)                     -- Get attribute value
xml.text(node)                           -- Get text content
```

### json.*
```lua
json.parse(text)                         -- Parse JSON string into Lua table
json.encode(table)                       -- Encode Lua table as JSON string
```

### system.*
```lua
system.sleep()                           -- Enter sleep mode
system.restart()                         -- Reboot device
system.freeHeap()                        -- Free heap in bytes
system.version()                         -- Runtime firmware version
system.batteryPercent()                   -- Battery level 0-100
system.millis()                          -- Uptime in milliseconds
system.delay(ms)                         -- Yield for N milliseconds
system.log(message)                      -- Log to serial output
```

### i18n.*
```lua
i18n.get(key)                            -- Get translated string for current language
i18n.setLanguage(code)                   -- Switch language ("EN", "HE", etc.)
i18n.getLanguage()                       -- Current language code
```

## Plugin Structure

### Plugin Discovery

On boot, the runtime scans `/plugins/` on the SD card for `.lua` files. Each plugin registers itself:

```lua
-- /plugins/sefaria.lua
plugin = {
  name = "Sefaria Browser",
  id = "sefaria",
  version = "1.0",
  author = "dcherrera",
  type = "activity",          -- "activity", "reader", "service"
  menuEntry = "Sefaria",      -- Text shown in home menu (nil = hidden)
  fileExtensions = {},         -- File types this plugin handles (for readers)
}

function plugin.onEnter()
  -- Called when plugin is activated
end

function plugin.onExit()
  -- Called when plugin is deactivated (cleanup)
end

function plugin.loop()
  -- Called every frame (handle input, render)
end
```

### Reader Plugins

Reader plugins register file extensions they handle:

```lua
-- /plugins/epub_reader.lua
plugin = {
  name = "EPUB Reader",
  id = "epub_reader",
  type = "reader",
  fileExtensions = { "epub" },
}

function plugin.canOpen(path)
  return storage.exists(path) and path:match("%.epub$")
end

function plugin.open(path)
  local archive = zip.open(path)
  -- Parse container.xml, spine, chapters...
  -- Build page layout...
end

function plugin.renderPage(pageNum)
  display.clear()
  -- Draw text blocks for current page
  display.refresh()
end

function plugin.getPageCount()
  return totalPages
end
```

### Boot Sequence

```
1. Hardware init (display, SD, GPIO, battery)
2. Load font loader + cache system
3. Initialize Lua interpreter
4. Register native API modules
5. Scan /plugins/ for .lua files
6. Load plugin manifests (plugin table from each file)
7. Build menu from activity plugins
8. Load last state (open book, settings)
9. Enter main loop (dispatch to active plugin)
```

## Font File Format (.cfont)

Binary format for SD-loadable fonts, matching the current in-memory structure:

```
Header:
  magic: "CFNT" (4 bytes)
  version: uint8
  fontName: null-terminated string
  fontSize: uint8
  style: uint8 (regular/bold/italic/bold-italic)
  ascender: int8
  descender: int8
  lineHeight: uint8
  glyphCount: uint16
  intervalCount: uint16
  groupCount: uint16
  compressed: bool

Intervals:
  [intervalCount x { first: uint32, last: uint32, offset: uint32 }]

Glyphs:
  [glyphCount x { width, height, advanceX, left, top, dataLength, dataOffset }]

Groups (if compressed):
  [groupCount x { compressedOffset, compressedSize, uncompressedSize, glyphCount, firstGlyphIndex }]

Bitmap data:
  [raw or compressed glyph bitmaps]

Kerning (optional):
  leftClassCount: uint16
  rightClassCount: uint16
  [leftClasses], [rightClasses], [kernMatrix]
```

A PC-side converter tool (`cfont-convert`) converts TTF → .cfont:
```bash
cfont-convert NotoSans-Regular.ttf --size 14 --2bit --compress -o NotoSans-14-Regular.cfont
```

## Constraints

### RAM Budget (~380KB)
- Lua interpreter state: ~50KB
- Lua script execution stack: ~20KB
- Font cache (LRU, 2-3 decompressed groups): ~30KB
- Framebuffer: 48KB
- WiFi stack (when active): ~50KB
- Available for plugin data: ~100-150KB

### Flash Budget (~6.5MB → ~500KB)
- Hardware drivers + HAL: ~100KB
- Lua 5.4 interpreter: ~100KB
- Native API bindings: ~100KB
- Font loader + decompressor: ~50KB
- Boot/plugin manager: ~50KB
- Zip/XML/JSON native helpers: ~50KB
- **Total: ~450-500KB**

### Performance
- Lua is ~10-50x slower than native C++ for computation
- For UI rendering: Lua calls native drawText/drawLine — the hot path is still C++
- Page layout (EPUB line breaking): may need optimization or native helpers for complex layouts
- E-ink refresh is the bottleneck (400ms-1.5s), not computation — Lua is fast enough

## Migration Path

This doesn't need to be all-or-nothing. Phased approach:

### Phase 1: Runtime Foundation
- Embed Lua 5.4 interpreter
- Implement core API modules (display, input, storage, system)
- Plugin discovery and lifecycle management
- Boot a simple "Hello World" plugin from SD

### Phase 2: Simple Plugins
- Migrate settings menus to Lua
- Migrate file browser to Lua
- Implement font loading from SD (.cfont format)
- Create cfont-convert tool

### Phase 3: Reader Plugins
- Migrate TXT reader to Lua
- Migrate MD reader to Lua
- Implement zip.* and xml.* APIs for EPUB support
- Migrate EPUB reader to Lua (biggest effort)

### Phase 4: Network Plugins
- Implement wifi.* API
- Migrate OPDS browser to Lua
- Migrate KOReader sync to Lua
- Migrate OTA updater to Lua
- Build Sefaria plugin

### Phase 5: Ecosystem
- Plugin repository / sharing mechanism
- Plugin versioning and dependency management
- Documentation for plugin developers
- Example plugins and templates

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| Lua interpreter uses too much RAM | High | Lua 5.4 is lean (~50KB state). Profile early. Consider LuaJIT if needed (but RISC-V support is limited). |
| EPUB layout too slow in Lua | Medium | Keep line-breaking algorithm as native helper callable from Lua. Only the parsing/flow logic is in Lua. |
| Plugin errors crash the device | Medium | Lua has protected calls (pcall). Wrap all plugin calls. Watchdog timer for runaway scripts. |
| SD card read latency slows rendering | Low | Font cache prevents repeated reads. Scripts loaded once into memory. |
| API surface too large to maintain | Medium | Start minimal. Add APIs as plugins need them. Version the API. |
| Fragmented ecosystem | Low | Ship core plugins (EPUB, TXT, settings) as defaults. Community plugins are optional extras. |

## Success Criteria

1. Core firmware fits in ~500KB flash
2. Default plugins (EPUB reader, file browser, settings) work identically to current native versions
3. A new plugin (Sefaria browser) can be built in pure Lua and dropped onto SD
4. Font files load from SD with no visible performance difference
5. RAM usage stays under 300KB during normal operation
6. Page turn latency is comparable to native (within 100ms)
7. Plugin errors are caught and don't crash the device

## Dependencies

- Lua 5.4 source (MIT license, ~25 files, ~30K lines of C)
- Existing CrossPoint HAL layer (HalDisplay, HalGPIO, HalStorage)
- cfont-convert tool (new, Python or C++)
- Hebrew/RTL support (Phases 1-4 of hebrew_build_plan.md)
