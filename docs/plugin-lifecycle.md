# Plugin Lifecycle

## Discovery

At boot, the plugin manager scans `/plugins/` on the SD card for `.lua` files. For each file found:

1. A temporary Lua state is created
2. The file is executed (this defines the `plugin` table)
3. The manifest fields are read: `name`, `id`, `type`, `menuEntry`, `fileExtensions`
4. The temporary state is closed (only metadata is kept, ~200 bytes per plugin)

Plugins without a valid `id` field are skipped with an error log.

Maximum 16 plugins can be discovered.

## Manifest Format

Every plugin must define a global `plugin` table:

```lua
plugin = {
    name = "My Plugin",          -- Human-readable name (required)
    id = "my_plugin",            -- Unique identifier (required)
    type = "activity",           -- "activity", "reader", or "service" (required)
    menuEntry = "My Tool",       -- Shown in home menu (nil = hidden)
    fileExtensions = {"txt"},    -- For reader plugins only
}
```

## Lifecycle Functions

```lua
function plugin.onEnter(arg)
    -- Called when plugin becomes active
    -- arg is optional (e.g., file path for readers)
end

function plugin.loop()
    -- Called every frame (~60Hz when awake)
    -- Handle input, update display
end

function plugin.onExit()
    -- Called when leaving the plugin
    -- Free resources, save state
end
```

All lifecycle functions are optional. If `loop()` is missing, the plugin runs `onEnter()` once and stays static until navigation.

## Navigation

Plugins can request navigation from within Lua:

```lua
plugin.finish()                    -- Return to home plugin
plugin.navigate("settings")        -- Switch to another plugin by ID
plugin.navigate("epub_reader", "/books/book.epub")  -- With argument
plugin.goHome()                    -- Explicit home navigation
```

Navigation is deferred — the flag is set during the current `loop()` call, and the actual switch happens before the next frame. This prevents mid-frame state corruption.

## Plugin Switching

When switching from plugin A to plugin B:

1. `plugin.onExit()` is called on A (via pcall)
2. A's Lua state is closed (all memory freed)
3. A fresh Lua state is created for B
4. B's `.lua` file is loaded and executed
5. Navigation functions are registered on B's `plugin` table
6. `plugin.onEnter(arg)` is called on B
7. Active plugin ID is saved to `/crosslua_state.txt`

Each plugin runs in complete isolation — separate Lua states, no shared global state.

## Error Handling

All plugin calls use `lua_pcall`:

- If `onEnter()` fails: plugin is stopped, error logged
- If `loop()` fails: plugin is stopped, error logged, screen cleared
- If `onExit()` fails: error logged, state closed anyway

Plugins cannot crash the runtime. Errors are contained and logged to serial.

## State Persistence

The active plugin ID is saved to `/crosslua_state.txt` on the SD card whenever a plugin is activated. On the next boot:

1. Plugin manager reads this file
2. If the saved plugin exists, it's started automatically
3. If not found, falls back to "home" plugin
4. If no "home" plugin, starts the first discovered plugin

## Reader Plugins

Reader plugins register file extensions they handle:

```lua
plugin = {
    type = "reader",
    fileExtensions = {"epub", "epub3"},
}
```

The file browser uses `plugin_manager_find_reader(extension)` to dispatch files to the correct reader plugin. The file path is passed as the `arg` to `onEnter()`.
