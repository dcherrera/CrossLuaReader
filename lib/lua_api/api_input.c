/**
 * @file api_input.c
 * @brief Lua input.* module: button state queries and constants.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include "api_input.h"
#include "lua.h"
#include "lauxlib.h"

#include "hal_gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* input.poll() — update button states. Call once per frame. */
static int l_input_poll(lua_State *L) {
    (void)L;
    hal_gpio_poll();
    return 0;
}

/* input.isPressed(button) → bool */
static int l_input_is_pressed(lua_State *L) {
    int btn = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, hal_gpio_is_pressed((uint8_t)btn));
    return 1;
}

/* input.wasPressed(button) → bool */
static int l_input_was_pressed(lua_State *L) {
    int btn = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, hal_gpio_was_pressed((uint8_t)btn));
    return 1;
}

/* input.wasAnyPressed() → bool */
static int l_input_was_any_pressed(lua_State *L) {
    lua_pushboolean(L, hal_gpio_was_any_pressed());
    return 1;
}

/* input.wasReleased(button) → bool */
static int l_input_was_released(lua_State *L) {
    int btn = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, hal_gpio_was_released((uint8_t)btn));
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

/* input.waitButton() → int (blocks until a button is pressed, returns button id) */
static int l_input_wait_button(lua_State *L) {
    while (1) {
        hal_gpio_poll();
        for (int i = 0; i < BTN_COUNT; i++) {
            if (hal_gpio_was_pressed((uint8_t)i)) {
                lua_pushinteger(L, i);
                return 1;
            }
        }
        vTaskDelay(1);
    }
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
