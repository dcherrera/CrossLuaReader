/**
 * @file boot_font.c
 * @brief Boot font storage. Loaded once at startup, never unloaded.
 *
 * @status Phase 8
 * @issues None
 * @todo None
 */

#include "boot_font.h"

static int boot_font_id = -1;

void boot_font_set_id(int id) {
    boot_font_id = id;
}

int boot_font_get_id(void) {
    return boot_font_id;
}
