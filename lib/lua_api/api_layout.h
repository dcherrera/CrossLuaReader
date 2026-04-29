/**
 * @file api_layout.h
 * @brief Lua layout.* module: exposes the layout engine to Lua plugins.
 *
 * @status Phase 1 — core bindings
 * @issues None
 * @todo None
 */
#pragma once

#include "lua.h"

/** Register layout.* functions with a Lua state. */
void api_layout_register(lua_State *L);
