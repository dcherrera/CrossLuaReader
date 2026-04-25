/**
 * @file api_font.h
 * @brief Lua API bindings for the font module (font.*).
 *        Exposes: load, unload.
 *
 * @status Complete
 * @issues None
 * @todo None
 */
#pragma once

#include "lua.h"

/** Register all font.* functions with the Lua state. */
void api_font_register(lua_State *L);
