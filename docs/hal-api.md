# HAL API Reference

## Overview

The Hardware Abstraction Layer provides pure C functions for accessing the Xteink X4/X3 hardware. All application code calls HAL functions — never the SDK classes directly.

### Architecture

```
Lua Plugins → Lua API Bindings → HAL (pure C) → Bridge (C++) → SDK (C++)
```

The bridge layer is thin `extern "C"` wrappers around SDK class methods. The HAL layer adds logging, caching, and initialization order management. Application code only sees the HAL.

### Init Order

```c
hal_system_init();   // 1. Watchdog, serial
hal_gpio_init();     // 2. SPI bus, buttons, device detection (MUST be before display/storage)
hal_display_init();  // 3. E-ink panel (uses SPI from step 2)
hal_storage_init();  // 4. SD card (uses SPI from step 2)
hal_power_init();    // 5. Battery monitor (uses device type from step 2)
```

---

## hal_system

System-level functions. Direct ESP-IDF calls, no bridge needed.

| Function | Returns | Description |
|----------|---------|-------------|
| `hal_system_init()` | `bool` | Initialize system hardware |
| `hal_system_restart()` | void | Reboot device (does not return) |
| `hal_system_free_heap()` | `uint32_t` | Current free heap (bytes) |
| `hal_system_total_heap()` | `uint32_t` | Total heap size (bytes) |
| `hal_system_min_free_heap()` | `uint32_t` | Minimum free heap since boot |
| `hal_system_uptime_ms()` | `uint32_t` | Milliseconds since boot |
| `hal_system_version()` | `const char*` | Firmware version string |

## hal_gpio

Button input and device detection. Initializes the shared SPI bus.

| Function | Returns | Description |
|----------|---------|-------------|
| `hal_gpio_init()` | `bool` | Init SPI, buttons, detect X3/X4 |
| `hal_gpio_poll()` | void | Update button states (call each loop) |
| `hal_gpio_is_pressed(btn)` | `bool` | Button currently held |
| `hal_gpio_was_pressed(btn)` | `bool` | Button pressed since last poll |
| `hal_gpio_was_any_pressed()` | `bool` | Any button pressed |
| `hal_gpio_was_released(btn)` | `bool` | Button released since last poll |
| `hal_gpio_was_any_released()` | `bool` | Any button released |
| `hal_gpio_get_held_time()` | `unsigned long` | Hold duration (ms) |
| `hal_gpio_get_device_type()` | `device_type_t` | `DEVICE_X4` or `DEVICE_X3` |
| `hal_gpio_is_x3()` | `bool` | Shorthand for X3 check |
| `hal_gpio_is_x4()` | `bool` | Shorthand for X4 check |
| `hal_gpio_start_deep_sleep()` | void | Enter deep sleep (does not return) |

**Button constants:** `BTN_BACK(0)`, `BTN_CONFIRM(1)`, `BTN_LEFT(2)`, `BTN_RIGHT(3)`, `BTN_UP(4)`, `BTN_DOWN(5)`, `BTN_POWER(6)`

## hal_display

E-ink display control.

| Function | Returns | Description |
|----------|---------|-------------|
| `hal_display_init()` | `bool` | Init display (auto-detects X3) |
| `hal_display_clear(color)` | void | Fill framebuffer (0xFF=white, 0x00=black) |
| `hal_display_refresh(mode)` | void | Push framebuffer to e-ink |
| `hal_display_deep_sleep()` | void | Display low-power mode |
| `hal_display_get_framebuffer()` | `uint8_t*` | Raw framebuffer pointer |
| `hal_display_width()` | `int` | Panel width (800 or 792) |
| `hal_display_height()` | `int` | Panel height (480 or 528) |
| `hal_display_width_bytes()` | `int` | Bytes per row (width/8) |
| `hal_display_buffer_size()` | `uint32_t` | Total framebuffer bytes |

**Refresh modes:** `REFRESH_FULL`, `REFRESH_HALF`, `REFRESH_FAST`

## hal_storage

SD card file I/O with opaque handles.

| Function | Returns | Description |
|----------|---------|-------------|
| `hal_storage_init()` | `bool` | Mount SD card |
| `hal_storage_ready()` | `bool` | SD card mounted? |
| `hal_storage_exists(path)` | `bool` | File/dir exists? |
| `hal_storage_mkdir(path)` | `bool` | Create directory |
| `hal_storage_remove(path)` | `bool` | Delete file |
| `hal_storage_rename(old, new)` | `bool` | Move/rename |
| `hal_storage_open(path, mode)` | `hal_file_t` | Open file (NULL on fail) |
| `hal_storage_file_read(f, buf, n)` | `int` | Read bytes (-1 on error) |
| `hal_storage_file_write(f, buf, n)` | `size_t` | Write bytes |
| `hal_storage_file_seek(f, pos)` | `bool` | Seek to position |
| `hal_storage_file_position(f)` | `size_t` | Current position |
| `hal_storage_file_size(f)` | `size_t` | File size |
| `hal_storage_file_available(f)` | `int` | Bytes remaining |
| `hal_storage_file_close(f)` | void | Close and free handle |
| `hal_storage_dir_open(path)` | `hal_dir_t` | Open directory |
| `hal_storage_dir_next(d, buf, sz, is_dir)` | `bool` | Next entry |
| `hal_storage_dir_close(d)` | void | Close directory |

**Modes:** `HAL_FILE_READ(0)`, `HAL_FILE_WRITE(1)`

## hal_power

Battery and sleep management.

| Function | Returns | Description |
|----------|---------|-------------|
| `hal_power_init()` | `bool` | Init battery monitor |
| `hal_power_battery_percent()` | `uint16_t` | Battery 0-100% (cached 30s) |
| `hal_power_battery_millivolts()` | `uint16_t` | Raw voltage |
| `hal_power_check_sleep()` | void | Auto-sleep if idle (call each loop) |
| `hal_power_enter_sleep()` | void | Sleep immediately (does not return) |

---

## Bridge Pattern

Each SDK class is wrapped by a single `.cpp` file with `extern "C"` functions:

```cpp
// bridge_display.cpp
#include <EInkDisplay.h>
static EInkDisplay display(8, 10, 21, 4, 5, 6);

extern "C" {
    void bridge_display_init(void) { display.begin(); }
    uint8_t *bridge_display_get_framebuffer(void) { return display.getFrameBuffer(); }
    // ...
}
```

The HAL `.c` files call these bridge functions via `extern` declarations — no header file needed for the bridge since it's an internal implementation detail.

## Renderer

The renderer operates on the framebuffer in logical coordinates with orientation transforms:

| Function | Description |
|----------|-------------|
| `renderer_init()` | Get framebuffer from display HAL |
| `renderer_set_orientation(orient)` | Set coordinate transform mode |
| `renderer_screen_width/height()` | Logical dimensions for current orientation |
| `renderer_draw_pixel(x, y, black)` | Single pixel with transform |
| `renderer_draw_line(x1, y1, x2, y2, black)` | Bresenham's + h/v optimization |
| `renderer_draw_rect(x, y, w, h, black)` | Rectangle outline |
| `renderer_fill_rect(x, y, w, h, black)` | Filled rectangle |
| `renderer_clear_screen(color)` | memset framebuffer |
| `renderer_get_viewable_margins(t, r, b, l)` | Rotated bezel margins |

**Orientations:** `ORIENT_PORTRAIT`, `ORIENT_LANDSCAPE_CW`, `ORIENT_PORTRAIT_INV`, `ORIENT_LANDSCAPE_CCW`
