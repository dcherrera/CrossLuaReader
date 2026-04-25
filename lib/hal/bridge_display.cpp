/**
 * @file bridge_display.cpp
 * @brief C++ bridge wrapping EInkDisplay SDK class for C HAL access.
 *        This is one of the few C++ files in the project — exists only
 *        because the SDK is C++.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include <EInkDisplay.h>

extern "C" {

/* SPI pin configuration for Xteink X4 */
#define EPD_SCLK 8
#define EPD_MOSI 10
#define EPD_CS   21
#define EPD_DC   4
#define EPD_RST  5
#define EPD_BUSY 6

static EInkDisplay display(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);

void bridge_display_set_x3(void) {
    display.setDisplayX3();
}

void bridge_display_init(void) {
    display.begin();
}

void bridge_display_clear(uint8_t color) {
    display.clearScreen(color);
}

void bridge_display_refresh(int mode, bool turn_off) {
    EInkDisplay::RefreshMode rm;
    switch (mode) {
        case 0:  rm = EInkDisplay::FULL_REFRESH; break;
        case 1:  rm = EInkDisplay::HALF_REFRESH; break;
        default: rm = EInkDisplay::FAST_REFRESH; break;
    }
    display.displayBuffer(rm, turn_off);
}

void bridge_display_deep_sleep(void) {
    display.deepSleep();
}

uint8_t *bridge_display_get_framebuffer(void) {
    return display.getFrameBuffer();
}

uint16_t bridge_display_get_width(void) {
    return display.getDisplayWidth();
}

uint16_t bridge_display_get_height(void) {
    return display.getDisplayHeight();
}

uint16_t bridge_display_get_width_bytes(void) {
    return display.getDisplayWidthBytes();
}

uint32_t bridge_display_get_buffer_size(void) {
    return display.getBufferSize();
}

void bridge_display_request_resync(uint8_t settle_passes) {
    display.requestResync(settle_passes);
}

}  /* extern "C" */
