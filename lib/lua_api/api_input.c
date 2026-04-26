/**
 * @file api_input.c
 * @brief Lua input.* module: button state queries with orientation remap.
 *
 * @status Phase 6 — orientation-aware button remapping
 * @issues None
 * @todo None
 */

#include "api_input.h"
#include "lua.h"
#include "lauxlib.h"

#include "hal_gpio.h"
#include "logging.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*
 * Button remap table: maps physical button → logical button.
 * When user presses physical button P, we report it as logical button remap[P].
 * Default is identity (physical == logical).
 * Set via input.setMapping() from Lua based on orientation.
 *
 * Logical buttons: BACK=0, CONFIRM=1, LEFT=2, RIGHT=3, UP=4, DOWN=5, POWER=6
 */
static uint8_t phys_to_logical[BTN_COUNT] = {0, 1, 2, 3, 4, 5, 6};

/**
 * Check if a logical button is pressed by scanning all physical buttons
 * and checking if any of them map to the requested logical button.
 */
static bool check_logical_pressed(int logical, bool (*check_fn)(uint8_t)) {
    for (int phys = 0; phys < BTN_COUNT; phys++) {
        if (phys_to_logical[phys] == (uint8_t)logical) {
            if (check_fn((uint8_t)phys)) return true;
        }
    }
    return false;
}

/* input.poll() — no-op, main loop already calls hal_gpio_poll().
 * Kept for API compatibility — plugins can call it but it does nothing. */
static int l_input_poll(lua_State *L) {
    (void)L;
    /* hal_gpio_poll() is called in main loop before dispatch_loop.
     * Calling it again here would clear the button edge states. */
    return 0;
}

/* input.isPressed(button) → bool — checks all physical buttons mapped to this logical */
static int l_input_is_pressed(lua_State *L) {
    int btn = (int)lua_tointeger(L, 1);
    lua_pushboolean(L, check_logical_pressed(btn, hal_gpio_is_pressed));
    return 1;
}

/* input.wasPressed(button) → bool — checks all physical buttons mapped to this logical */
static int l_input_was_pressed(lua_State *L) {
    int btn = (int)lua_tointeger(L, 1);
    lua_pushboolean(L, check_logical_pressed(btn, hal_gpio_was_pressed));
    return 1;
}

/* input.wasAnyPressed() → bool */
static int l_input_was_any_pressed(lua_State *L) {
    lua_pushboolean(L, hal_gpio_was_any_pressed());
    return 1;
}

/* input.wasReleased(button) → bool — checks all physical buttons mapped to this logical */
static int l_input_was_released(lua_State *L) {
    int btn = (int)lua_tointeger(L, 1);
    lua_pushboolean(L, check_logical_pressed(btn, hal_gpio_was_released));
    return 1;
}

/* input.wasAnyReleased() → bool */
static int l_input_was_any_released(lua_State *L) {
    lua_pushboolean(L, hal_gpio_was_any_released());
    return 1;
}

/* input.getHeldTime() → int (milliseconds) */
static int l_input_get_held_time(lua_State *L) {
    lua_pushinteger(L, (lua_Integer)hal_gpio_get_held_time());
    return 1;
}

/* input.waitButton() → int (blocks until a button is pressed, returns logical button id) */
static int l_input_wait_button(lua_State *L) {
    while (1) {
        hal_gpio_poll();
        /* Check all physical buttons and return the logical mapping */
        for (int phys = 0; phys < BTN_COUNT; phys++) {
            if (hal_gpio_was_pressed((uint8_t)phys)) {
                lua_pushinteger(L, phys_to_logical[phys]);
                return 1;
            }
        }
        vTaskDelay(1);
    }
}

/*
 * input.setMapping(table) — set button remap from orientation layout.
 * Table format: {back=0, confirm=1, left=2, right=3, up=4, down=5}
 * Values are logical→physical: "back" (logical BACK) maps to physical hw index.
 * We INVERT this to build phys_to_logical: phys_to_logical[phys] = logical.
 */
static int l_input_set_mapping(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    /* Reset to identity: physical N → logical N */
    for (int i = 0; i < BTN_COUNT; i++) phys_to_logical[i] = (uint8_t)i;

    /* Logical button indices (must match BTN_* constants in hal_gpio.h) */
    static const struct { const char *name; uint8_t logical; } fields[] = {
        {"back",    BTN_BACK},
        {"confirm", BTN_CONFIRM},
        {"left",    BTN_LEFT},
        {"right",   BTN_RIGHT},
        {"up",      BTN_UP},
        {"down",    BTN_DOWN},
    };

    /* Read logical→physical from Lua table, invert to physical→logical */
    for (int i = 0; i < 6; i++) {
        lua_getfield(L, 1, fields[i].name);
        if (!lua_isnil(L, -1)) {
            int phys = (int)lua_tointeger(L, -1);
            if (phys >= 0 && phys < BTN_COUNT) {
                phys_to_logical[phys] = fields[i].logical;
            }
        }
        lua_pop(L, 1);
    }

    LOG_INF("INPUT", "phys_to_logical: [%d,%d,%d,%d,%d,%d,%d]",
            phys_to_logical[0], phys_to_logical[1], phys_to_logical[2],
            phys_to_logical[3], phys_to_logical[4], phys_to_logical[5],
            phys_to_logical[6]);

    return 0;
}

/* input.resetMapping() — reset to identity (no remap) */
static int l_input_reset_mapping(lua_State *L) {
    (void)L;
    for (int i = 0; i < BTN_COUNT; i++) phys_to_logical[i] = (uint8_t)i;
    LOG_INF("INPUT", "Remap reset to identity");
    return 0;
}

void api_input_register(lua_State *L) {
    static const luaL_Reg funcs[] = {
        {"poll",           l_input_poll},
        {"isPressed",      l_input_is_pressed},
        {"wasPressed",     l_input_was_pressed},
        {"wasAnyPressed",  l_input_was_any_pressed},
        {"wasReleased",    l_input_was_released},
        {"wasAnyReleased", l_input_was_any_released},
        {"getHeldTime",    l_input_get_held_time},
        {"waitButton",     l_input_wait_button},
        {"setMapping",     l_input_set_mapping},
        {"resetMapping",   l_input_reset_mapping},
        {NULL, NULL}
    };
    luaL_newlib(L, funcs);

    /* Button constants */
    lua_pushinteger(L, BTN_BACK);    lua_setfield(L, -2, "BACK");
    lua_pushinteger(L, BTN_CONFIRM); lua_setfield(L, -2, "CONFIRM");
    lua_pushinteger(L, BTN_LEFT);    lua_setfield(L, -2, "LEFT");
    lua_pushinteger(L, BTN_RIGHT);   lua_setfield(L, -2, "RIGHT");
    lua_pushinteger(L, BTN_UP);      lua_setfield(L, -2, "UP");
    lua_pushinteger(L, BTN_DOWN);    lua_setfield(L, -2, "DOWN");
    lua_pushinteger(L, BTN_POWER);   lua_setfield(L, -2, "POWER");

    lua_setglobal(L, "input");
}
