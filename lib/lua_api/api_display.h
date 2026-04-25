/**
 * @file api_display.h
 * @brief Lua API bindings for the display module (display.*).
 *        Exposes: clear, drawText, drawLine, drawRect, fillRect,
 *        refresh, width, height, getTextWidth, getLineHeight.
 *
 * @status Complete
 * @issues None
 * @todo None
 */
#pragma once

#include "lua.h"

/** Register all display.* functions with the Lua state. */
void api_display_register(lua_State *L);
