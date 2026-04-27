/**
 * @file api_input.h
 * @brief Lua API bindings for the input module (input.*).
 *        Exposes: poll, isPressed, wasPressed, wasReleased, getHeldTime,
 *        and button constants (BACK, CONFIRM, LEFT, RIGHT, UP, DOWN, POWER).
 *
 * @status Complete
 * @issues None
 * @todo None
 */
#pragma once

#include "lua.h"

/** Register all input.* functions and constants with the Lua state. */
void api_input_register(lua_State *L);

/** Scan physical buttons and push new presses into the event queue. */
void api_input_scan_to_queue(void);

/** Start background input polling task (call once after hal_gpio_init). */
void api_input_start_task(void);
