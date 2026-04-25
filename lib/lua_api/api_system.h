/**
 * @file api_system.h
 * @brief Lua API bindings for the system module (system.*).
 *        Exposes: freeHeap, battery, millis, delay, log, version, restart.
 *
 * @status Complete
 * @issues None
 * @todo None
 */
#pragma once

#include "lua.h"

/** Register all system.* functions with the Lua state. */
void api_system_register(lua_State *L);
