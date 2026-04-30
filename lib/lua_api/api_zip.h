/**
 * @file api_zip.h
 * @brief Lua zip.* module — opt-in capability bound by api_register_capability
 *        when a plugin manifest declares requires = {"zip"}.
 *
 * @status Phase 9.A.1
 * @issues None
 * @todo None
 */
#pragma once

#include "lua.h"

/** Register zip.* into the given Lua state. */
void api_zip_register(lua_State *L);
