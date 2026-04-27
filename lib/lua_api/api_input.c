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
#include "esp_timer.h"

/*
 * Button remap table: maps physical button → logical button.
 * When user presses physical button P, we report it as logical button remap[P].
 * Default is identity (physical == logical).
 * Set via input.setMapping() from Lua based on orientation.
 *
 * Logical buttons: BACK=0, CONFIRM=1, LEFT=2, RIGHT=3, UP=4, DOWN=5, POWER=6
 */
static uint8_t phys_to_logical[BTN_COUNT] = {0, 1, 2, 3, 4, 5, 6};

/*
 * Input event FIFO — accumulates button press events so presses during
 * blocking e-ink refresh aren't lost. Each press is stored as a logical
 * button ID. Plugins consume one event at a time via wasPressed().
 */
#define INPUT_QUEUE_SIZE 16
static uint8_t input_queue[INPUT_QUEUE_SIZE];
static int input_queue_head = 0;
static int input_queue_tail = 0;

static int input_queue_count(void) {
    return (input_queue_tail - input_queue_head + INPUT_QUEUE_SIZE) % INPUT_QUEUE_SIZE;
}

static void input_queue_push(uint8_t logical_btn) {
    int next = (input_queue_tail + 1) % INPUT_QUEUE_SIZE;
    if (next == input_queue_head) return;  /* full, drop oldest would be better but keep simple */
    input_queue[input_queue_tail] = logical_btn;
    input_queue_tail = next;
}

static bool input_queue_pop(uint8_t logical_btn) {
    /* Scan queue for this button, remove first match */
    int i = input_queue_head;
    while (i != input_queue_tail) {
        if (input_queue[i] == logical_btn) {
            /* Remove by shifting remaining elements */
            int j = i;
            while (j != input_queue_tail) {
                int next = (j + 1) % INPUT_QUEUE_SIZE;
                if (next == input_queue_tail) break;
                input_queue[j] = input_queue[next];
                j = next;
            }
            input_queue_tail = (input_queue_tail - 1 + INPUT_QUEUE_SIZE) % INPUT_QUEUE_SIZE;
            return true;
        }
        i = (i + 1) % INPUT_QUEUE_SIZE;
    }
    return false;
}

/**
 * Scan physical buttons and push any new presses into the event queue.
 */
static void scan_to_queue(void) {
    for (int phys = 0; phys < BTN_COUNT; phys++) {
        if (hal_gpio_was_pressed((uint8_t)phys)) {
            input_queue_push(phys_to_logical[phys]);
        }
    }
}

/* Keep public API for any external callers */
void api_input_scan_to_queue(void) {
    scan_to_queue();
}

/*
 * Background input polling via esp_timer.
 * Fires every 5ms on the system timer task — no dedicated FreeRTOS
 * task needed, zero heap allocation. Polls buttons and pushes
 * press events into the queue regardless of main loop state.
 */
static esp_timer_handle_t input_timer = NULL;

static void input_timer_cb(void *arg) {
    (void)arg;
    hal_gpio_poll();
    scan_to_queue();
}

/**
 * Start background input polling via hardware timer.
 * Call once during init, after hal_gpio_init().
 */
void api_input_start_task(void) {
    if (input_timer) return;

    esp_timer_create_args_t args = {
        .callback = input_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "input_poll",
    };

    if (esp_timer_create(&args, &input_timer) == ESP_OK) {
        esp_timer_start_periodic(input_timer, 5000);  /* 5ms = 5000us */
        LOG_INF("INPUT", "Background input timer started (5ms)");
    } else {
        LOG_ERR("INPUT", "Failed to create input timer");
    }
}

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

/* input.wasPressed(button) → bool — checks queue first, then live state */
static int l_input_was_pressed(lua_State *L) {
    int btn = (int)lua_tointeger(L, 1);
    /* Check queued events first (from presses during refresh) */
    if (input_queue_pop((uint8_t)btn)) {
        lua_pushboolean(L, 1);
        return 1;
    }
    /* Fall back to live state */
    lua_pushboolean(L, check_logical_pressed(btn, hal_gpio_was_pressed));
    return 1;
}

/* input.wasAnyPressed() → bool — checks queue + live state */
static int l_input_was_any_pressed(lua_State *L) {
    lua_pushboolean(L, input_queue_count() > 0 || hal_gpio_was_any_pressed());
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
