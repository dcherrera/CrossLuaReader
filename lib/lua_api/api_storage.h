/**
 * @file api_storage.h
 * @brief Lua API bindings for the storage module (storage.*).
 *        Exposes: read, write, exists, mkdir, remove, list, fileSize.
 *
 * @status Complete
 * @issues None
 * @todo None
 */
#pragma once

#include "lua.h"

/** Register all storage.* functions with the Lua state. */
void api_storage_register(lua_State *L);
