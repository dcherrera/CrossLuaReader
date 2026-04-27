/**
 * @file api_text.h
 * @brief Lua text.* module: C-side text indexing and page layout.
 *        Streams files from SD, never loads entire file into RAM.
 *
 * @status Phase 9
 * @issues None
 * @todo None
 */
#pragma once

#include "lua.h"

/** Register text.* functions with a Lua state. */
void api_text_register(lua_State *L);
