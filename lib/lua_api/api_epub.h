/**
 * @file api_epub.h
 * @brief Lua epub.* module — opt-in capability bound by api_register_capability
 *        when a plugin manifest declares requires = {"epub"}.
 *
 * @status Phase 9.A.3
 * @issues None
 * @todo None
 */
#pragma once

#include "lua.h"

/** Register epub.* into the given Lua state. */
void api_epub_register(lua_State *L);
