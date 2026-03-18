# Power Management Design

## Goal

Reduce power consumption on battery-powered builds by dimming, then turning off, then light-sleeping after user inactivity. Mains-powered builds are unaffected.

## Architecture

A new `power_mgr` module owns a 4-state inactivity state machine. All power management code is gated behind `#ifdef BATTERY_MODE` and compiled away in mains builds.

## State Machine

```
ACTIVE → DIM (2 min no touch)
DIM    → OFF  (5 min total)
OFF    → SLEEP (10 min total)
any state + touch → ACTIVE
```

Backlight levels: ACTIVE=100%, DIM=20%, OFF=0%, SLEEP=0%.

Timeouts are measured from the last touch event using `millis()`.

## Module Interface

**`src/power_mgr.h`**
```cpp
#pragma once
#ifdef BATTERY_MODE
void power_mgr_init();   // register touch callback, set initial state
void power_mgr_touch();  // reset inactivity timer, restore backlight if dimmed
void power_mgr_tick();   // drive state machine; call from LVGL main loop
#endif
```

**`src/power_mgr.cpp`** — all content inside `#ifdef BATTERY_MODE`.

## Touch Detection

`power_mgr_init()` registers an LVGL indev feedback callback on the touch input device via `lv_indev_set_feedback_cb()`. This fires on every touch regardless of which widget is hit. Requires `touch_get_indev()` to be added to `touch.h/.cpp` returning the registered `lv_indev_t*`.

## Backlight

`backlight_set()` in `display.cpp` is currently `static`. Promote to `display_backlight_set(int percent)` declared in `display.h`. Used by `power_mgr` to dim and restore the backlight.

## Light Sleep Entry (10 min)

Called from `power_mgr_tick()` on the UI task:

1. `net_task_cancel()` + `img_task_flush()` — stop in-flight fetches
2. `fetch_disconnect()` + `image_fetch_disconnect()` — close TLS connections
3. `WiFi.disconnect(true)` — disconnect but retain credentials
4. Configure GPIO4 (GT911 SDA) as wake source: level LOW, internal pull-up (touch generates I2C START, pulls SDA low)
5. `esp_light_sleep_start()` — CPU halts, PSRAM/RAM retained

## Wake Sequence

Returns from `esp_light_sleep_start()` on touch:

1. Reset inactivity timer → ACTIVE state
2. `display_backlight_set(100)`
3. `WiFi.reconnect()` — existing WiFi monitor banner handles reconnecting UI

## Build Configuration

`platformio.ini` gains a `[env:jc3248w535c_battery]` environment that inherits from `[env:jc3248w535c]` and adds `-DBATTERY_MODE` to `build_flags`.

## Files Changed

| File | Change |
|------|--------|
| `src/power_mgr.h` | New — public interface |
| `src/power_mgr.cpp` | New — state machine + sleep logic |
| `src/display.h` | Add `display_backlight_set()` declaration |
| `src/display.cpp` | Promote `backlight_set()` to `display_backlight_set()` |
| `src/touch.h` | Add `touch_get_indev()` declaration |
| `src/touch.cpp` | Expose stored `lv_indev_t*` |
| `src/ui_task.cpp` | Add `power_mgr_init/tick` calls under `#ifdef BATTERY_MODE` |
| `platformio.ini` | Add `[env:jc3248w535c_battery]` |

## Out of Scope

- CPU frequency scaling
- WiFi modem sleep during active use
- Deep sleep (touch INT not connected on this board)
- Battery level monitoring
