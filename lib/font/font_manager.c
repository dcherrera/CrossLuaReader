/**
 * @file font_manager.c
 * @brief Multi-font management implementation. Fixed-slot array of loaded
 *        fonts, each with a font_data_t and cached path string.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include "font_manager.h"
#include "font_loader.h"
#include "logging.h"

#include <string.h>

typedef struct {
    font_data_t font;
    char        path[FONT_MAX_PATH];
    bool        loaded;
} font_slot_t;

static font_slot_t slots[FONT_MAX_LOADED];

int font_manager_load(const char *path) {
    if (!path) return -1;

    /* Find empty slot */
    int slot_id = -1;
    for (int i = 0; i < FONT_MAX_LOADED; i++) {
        if (!slots[i].loaded) {
            slot_id = i;
            break;
        }
    }

    if (slot_id < 0) {
        LOG_ERR("FMGR", "No free font slots (max %d)", FONT_MAX_LOADED);
        return -1;
    }

    if (!font_loader_load(path, &slots[slot_id].font)) {
        return -1;
    }

    strncpy(slots[slot_id].path, path, FONT_MAX_PATH - 1);
    slots[slot_id].path[FONT_MAX_PATH - 1] = '\0';
    slots[slot_id].loaded = true;

    LOG_INF("FMGR", "Font %d loaded: %s", slot_id, path);
    return slot_id;
}

void font_manager_unload(int font_id) {
    if (font_id < 0 || font_id >= FONT_MAX_LOADED) return;
    if (!slots[font_id].loaded) return;

    font_loader_unload(&slots[font_id].font);
    slots[font_id].path[0] = '\0';
    slots[font_id].loaded = false;

    LOG_INF("FMGR", "Font %d unloaded", font_id);
}

const font_data_t *font_manager_get(int font_id) {
    if (font_id < 0 || font_id >= FONT_MAX_LOADED) return NULL;
    if (!slots[font_id].loaded) return NULL;
    return &slots[font_id].font;
}

const char *font_manager_get_path(int font_id) {
    if (font_id < 0 || font_id >= FONT_MAX_LOADED) return NULL;
    if (!slots[font_id].loaded) return NULL;
    return slots[font_id].path;
}

void font_manager_unload_all(void) {
    for (int i = 0; i < FONT_MAX_LOADED; i++) {
        font_manager_unload(i);
    }
}
