/**
 * @file api_register.h
 * @brief Lua state creation and API module registration.
 *        Splits registration into a core set (always registered) and
 *        opt-in capabilities (registered only when a plugin's manifest
 *        `requires = {...}` lists them).
 *
 * @status Complete
 * @issues None
 * @todo None
 */
#pragma once

#include "lua.h"

/**
 * Create a new Lua state with safe standard libraries opened and the SD-card
 * `require` searcher installed. The CORE API set (display, input, storage,
 * system, font, layout) is registered automatically. Opt-in capabilities
 * (text, zip, xml, epub, css, image, ...) are NOT — the caller must register
 * them via api_register_capability() based on the plugin's `requires`.
 *
 * @return New Lua state, or NULL on failure. Caller must lua_close().
 */
lua_State *api_create_state(void);

/**
 * Register the always-on CORE API modules into an existing Lua state.
 * Called automatically by api_create_state(); exposed for callers that
 * need to register core into an externally-created state.
 *
 * Registers: display, input, storage, system, font, layout.
 *
 * @param L Lua state
 */
void api_register_core(lua_State *L);

/**
 * Register an opt-in capability into an existing Lua state. Called by the
 * plugin manager once per entry in a plugin's `requires` manifest field.
 * Unknown capability names are logged and ignored (forward compatibility).
 *
 * Currently recognized capability names: "text", "zip", "xml".
 * Phases 9.A-9.D will add: "epub", "css", "image".
 *
 * @param L   Lua state
 * @param cap Capability name (NUL-terminated)
 */
void api_register_capability(lua_State *L, const char *cap);
