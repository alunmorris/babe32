# Power Management Design

## Goal

Reduce power consumption on battery-powered builds by dimming, then turning off the backlight, then entering a low-power polling loop after user inactivity. Mains-powered builds are unaffected.

## `#ifdef BATTERY_MODE`

All power management behaviour is compiled only when `-DBATTERY_MODE` is set. The `display_backlight_set()` rename is unconditional (pure refactor). `power_mgr_init()` calls `esp_wifi_set_ps(WIFI_PS_MIN_MODEM)` to enable WiFi modem sleep — battery-build only.

## State Machine

```
ACTIVE → DIM       (2 min from last touch)
DIM    → OFF       (5 min from last touch — 3 min after DIM)
OFF    → LOW_POWER (10 min from last touch — 5 min after OFF)
any state + touch → ACTIVE
```

Backlight levels: ACTIVE=100%, DIM=20%, OFF=0%, LOW_POWER=0%.
All thresholds are total elapsed time since last touch, measured with `millis()`.

## Touch Detection

A flag `s_touch_active` is set inside `touch_read_cb()` in `touch.cpp` (the LVGL indev read callback) when a touch is detected. `touch_was_active()` returns and clears the flag.

`power_mgr_tick()` calls `touch_was_active()` each call to reset the inactivity timer.

**Note:** The touch chip on JC3248W535C is the AXS15231B (not GT911). The existing `touch_read_cb()` already reads it via `axs_read()` over I2C. The `s_touch_active` flag is set in `touch_read_cb()` alongside the existing read logic.

Add to `touch.cpp/.h`:
- `static volatile bool s_touch_active = false;` set in `touch_read_cb()` when touch count > 0
- `bool touch_was_active()` — returns and clears `s_touch_active`

## Backlight

`backlight_set()` in `display.cpp` is currently `static` with no external call sites. Rename to `display_backlight_set(int percent)` and declare in `display.h`. Uses LEDC (`LEDC_LOW_SPEED_MODE`, `LEDC_AUTO_CLK`) — no LVGL lock required. LEDC resumes correct state after light sleep automatically with these settings.

## `power_mgr_tick()` Calling Context

Called from `ui_task.cpp` (core 1) **outside** `lvgl_lock()`, after the LVGL block, alongside the WiFi monitor and image result poll. `s_lvgl_mutex` is never held when `esp_light_sleep_start()` is invoked — no mutex deadlock possible.

- DIM/OFF transitions: call `display_backlight_set()` directly
- LOW_POWER transition: enter polling loop (already outside lock)

## Low-Power Polling Loop Entry

Before entering the polling loop:

1. `net_task_cancel()` — signal fetch to stop (sets cancel flag)
2. `fetch_disconnect()` — explicitly close the proxy TCP socket
3. `image_fetch_disconnect()` — close the image TLS socket
4. `img_task_flush()` — drain image queues
5. `esp_wifi_set_ps(WIFI_PS_MIN_MODEM)` — modem sleep during light sleep

Sockets are explicitly closed (steps 2–3) rather than relying on cancel-flag-only shutdown, so TCP state is clean before sleep. On wake, fetches that were in progress will fail with connection errors and show the existing Retry page.

## Low-Power Polling Loop

The polling loop runs synchronously on ui_task (core 1). `esp_light_sleep_start()` returns synchronously on wake before the FreeRTOS scheduler resumes, so the post-wake I2C read happens before `lv_timer_handler()` fires — no I2C re-entrancy hazard.

**I2C after light sleep:** ESP32-S3 light sleep preserves peripheral state (unlike deep sleep). APB clock and I2C peripheral resume on wake without re-initialisation. `Wire` / `axs_read()` is safe to call immediately after `esp_light_sleep_start()` returns.

```
loop:
  esp_sleep_enable_timer_wakeup(30 * 1000000ULL)
  esp_light_sleep_start()         // both cores halt; modem sleep active
  // wakes here — peripheral state preserved, FreeRTOS not yet resumed
  call axs_read() directly via Wire (I2C safe after light sleep)
  if touch detected:
    display_backlight_set(100)
    reset inactivity timer → ACTIVE
    return
  else:
    loop
```

**LVGL timers on wake:** Pending timers (100 ms invalidate, 500 ms KB counter, 2 s WiFi check) will fire immediately when `lv_timer_handler()` next runs — this is benign.

## Module Interface

**`src/power_mgr.h`**
```cpp
#pragma once
#ifdef BATTERY_MODE
void power_mgr_init();  // configure WiFi modem sleep
void power_mgr_tick();  // drive state machine; call outside lvgl_lock
#endif
```

**`src/power_mgr.cpp`** — entire file inside `#ifdef BATTERY_MODE ... #endif`.

## Build Configuration

```ini
[env:jc3248w535c_battery]
extends = jc3248w535c
build_flags =
    ${jc3248w535c.build_flags}
    -DBATTERY_MODE
```

## Files Changed

| File | Change |
|------|--------|
| `src/power_mgr.h` | New |
| `src/power_mgr.cpp` | New |
| `src/display.h` | Add `display_backlight_set()` declaration |
| `src/display.cpp` | Rename `backlight_set()` → `display_backlight_set()` |
| `src/touch.h` | Add `touch_was_active()` declaration |
| `src/touch.cpp` | Add `s_touch_active` flag in read callback, expose via `touch_was_active()` |
| `src/ui_task.cpp` | Call `power_mgr_init()` at startup and `power_mgr_tick()` in loop, both under `#ifdef BATTERY_MODE` |
| `platformio.ini` | Add `[env:jc3248w535c_battery]` |

## Out of Scope

- CPU frequency scaling
- Deep sleep (touch INT not connected on this board)
- Battery level monitoring
