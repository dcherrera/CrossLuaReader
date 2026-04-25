/**
 * @file api_register.h
 * @brief Register all CrossLua API modules with a Lua state.
 *        Opens standard Lua libs (minus io/os) and registers
 *        display, input, storage, system, and font modules.
 *
 * @status Complete
 * @issues None
 * @todo None
 */
#pragma once

#include "lua.h"

/**
 * Create a new Lua state with all CrossLua API modules registered.
 * Opens safe standard libraries (base, string, table, math, utf8)
 * and registers display.*, input.*, storage.*, system.*, font.*.
 *
 * @return New Lua state, or NULL on failure. Caller must lua_close().
 */
lua_State *api_create_state(void);

/**
 * Register all CrossLua API modules with an existing Lua state.
 *
 * @param L Lua state
 */
void api_register_all(lua_State *L);
