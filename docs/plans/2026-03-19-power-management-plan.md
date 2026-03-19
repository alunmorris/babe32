# Power Management Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add battery-mode power management — backlight dim/off/low-power sleep after inactivity — compiled only when `-DBATTERY_MODE` is set.

**Architecture:** A new `power_mgr` module drives a 4-state machine (ACTIVE→DIM→OFF→LOW_POWER) called from `ui_task`'s main loop outside the LVGL lock. Touch activity is tracked via a flag set in the existing LVGL touch callback. Light sleep is used for LOW_POWER; the ESP32-S3 preserves I2C/LEDC peripheral state across light sleep, so no re-initialisation is needed on wake.

**Tech Stack:** ESP32-S3, Arduino/FreeRTOS, LVGL 8.4.0, esp-idf esp_sleep, LEDC PWM backlight, AXS15231B I2C touch, PlatformIO.

---

## Chunk 1: Backlight and touch primitives

### Task 1: Promote `backlight_set` to `display_backlight_set`

**Files:**
- Modify: `src/display.cpp` (line 141)
- Modify: `src/display.h`

The static `backlight_set()` in `display.cpp` must become a public function so `power_mgr` can call it.

- [ ] **Step 1: Remove `static`, rename in display.cpp**

  Change line 141 from:
  ```cpp
  static void backlight_set(int percent) {
  ```
  to:
  ```cpp
  void display_backlight_set(int percent) {
  ```

- [ ] **Step 2: Update the call site at line 150**

  Change:
  ```cpp
      backlight_set(100);  // on immediately — if screen stays dark, we're crashing below
  ```
  to:
  ```cpp
      display_backlight_set(100);  // on immediately — if screen stays dark, we're crashing below
  ```

- [ ] **Step 3: Add declaration to display.h**

  After the existing declarations, add:
  ```cpp
  // Set backlight brightness 0-100%. Safe to call outside LVGL lock (LEDC, no LVGL).
  void display_backlight_set(int percent);
  ```

- [ ] **Step 4: Build default env to verify no regressions**

  ```bash
  /home/alun/.platformio/penv/bin/platformio run -e jc3248w535c 2>&1 | tail -5
  ```
  Expected: `SUCCESS`

- [ ] **Step 5: Commit**

  ```bash
  git add src/display.cpp src/display.h
  git commit -m "Promote backlight_set to display_backlight_set (public API)"
  ```

---

### Task 2: Add touch activity flag to touch.cpp

**Files:**
- Modify: `src/touch.cpp`
- Modify: `src/touch.h`

`power_mgr_tick()` needs to know if touch occurred since last call. A volatile flag set inside the existing `touch_read_cb()` and cleared on read is the right pattern — no LVGL API needed.

- [ ] **Step 1: Add flag and accessor to touch.cpp**

  After line 15 (`static bool s_touch_ok = false;`), add:
  ```cpp
  static volatile bool s_touch_active = false;
  ```

  Inside `touch_read_cb()`, after `g_touch_pressed = true;` (line 52), add:
  ```cpp
          s_touch_active = true;
  ```

  After `touch_lvgl_init()` (at end of file), add:
  ```cpp

  bool touch_was_active() {
      if (!s_touch_active) return false;
      s_touch_active = false;
      return true;
  }
  ```

- [ ] **Step 2: Declare in touch.h**

  Add after existing declarations:
  ```cpp
  // Returns true (and clears the flag) if touch occurred since last call.
  bool touch_was_active();
  ```

- [ ] **Step 3: Build to verify**

  ```bash
  /home/alun/.platformio/penv/bin/platformio run -e jc3248w535c 2>&1 | tail -5
  ```
  Expected: `SUCCESS`

- [ ] **Step 4: Commit**

  ```bash
  git add src/touch.cpp src/touch.h
  git commit -m "Add touch_was_active() flag for power manager"
  ```

---

## Chunk 2: Power manager module

### Task 3: Create power_mgr.h and power_mgr.cpp

**Files:**
- Create: `src/power_mgr.h`
- Create: `src/power_mgr.cpp`

The entire implementation is inside `#ifdef BATTERY_MODE`. The LOW_POWER polling loop runs synchronously on ui_task (core 1), blocking until touch is detected. `esp_light_sleep_start()` halts both cores and returns synchronously on wake — FreeRTOS does not resume until after it returns, so the Wire I2C call immediately after wake is safe (no re-entrancy). LEDC and I2C peripheral state is preserved by ESP32-S3 light sleep.

- [ ] **Step 1: Write power_mgr.h**

  ```cpp
  // 190326 Battery power management — dim, backlight off, light sleep
  #pragma once
  #ifdef BATTERY_MODE
  void power_mgr_init();   // call once at startup; enables WiFi modem sleep
  void power_mgr_tick();   // drive state machine; call outside lvgl_lock each loop
  #endif
  ```

- [ ] **Step 2: Write power_mgr.cpp**

  ```cpp
  // 190326 Battery power management — 4-state machine + light sleep polling loop
  #ifdef BATTERY_MODE

  #include "power_mgr.h"
  #include "display.h"
  #include "touch.h"
  #include "net_task.h"
  #include "fetcher.h"
  #include "image_fetch.h"
  #include "img_task.h"
  #include <Arduino.h>
  #include <Wire.h>
  #include <esp_sleep.h>
  #include <esp_wifi.h>

  // Thresholds — total elapsed ms since last touch
  #define DIM_MS  (2UL  * 60UL * 1000UL)   // 2 min
  #define OFF_MS  (5UL  * 60UL * 1000UL)   // 5 min
  #define LOW_MS  (10UL * 60UL * 1000UL)   // 10 min

  #define AXS_ADDR 0x3B

  enum PowerState { ACTIVE, DIM, OFF, LOW_POWER };
  static PowerState s_state        = ACTIVE;
  static uint32_t   s_last_touch_ms = 0;

  // Direct AXS15231B poll — used inside the light-sleep loop where LVGL is not running.
  // Wire is safe immediately after esp_light_sleep_start() returns (I2C peripheral preserved).
  static bool axs_poll_touch() {
      uint8_t cmd[8] = {0xb5, 0xab, 0xa5, 0x5a, 0, 0, 0, 0x08};
      Wire.beginTransmission(AXS_ADDR);
      Wire.write(cmd, 8);
      if (Wire.endTransmission() != 0) return false;
      if (Wire.requestFrom((uint8_t)AXS_ADDR, (uint8_t)14) < 14) return false;
      uint8_t buf[14];
      for (int i = 0; i < 14; i++) buf[i] = Wire.read();
      return (buf[0] == 0 && buf[1] > 0 && buf[1] <= 2);
  }

  void power_mgr_init() {
      esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
      s_last_touch_ms = millis();
  }

  void power_mgr_tick() {
      // Reset timer if touch occurred (flag set in touch_read_cb, cleared here)
      if (touch_was_active()) {
          s_last_touch_ms = millis();
          if (s_state != ACTIVE) {
              s_state = ACTIVE;
              display_backlight_set(100);
          }
          return;
      }

      uint32_t elapsed = millis() - s_last_touch_ms;

      if (s_state == ACTIVE && elapsed >= DIM_MS) {
          s_state = DIM;
          display_backlight_set(20);

      } else if (s_state == DIM && elapsed >= OFF_MS) {
          s_state = OFF;
          display_backlight_set(0);

      } else if (s_state == OFF && elapsed >= LOW_MS) {
          s_state = LOW_POWER;

          // Cleanly shut down in-flight network activity before sleeping
          net_task_cancel();          // signal fetch to stop
          fetch_disconnect();         // close proxy TCP socket
          image_fetch_disconnect();   // close image TLS socket
          img_task_flush();           // drain image queues

          // Polling loop — wakes every 30s to check touch via I2C
          for (;;) {
              esp_sleep_enable_timer_wakeup(30ULL * 1000000ULL);
              esp_light_sleep_start();
              // Both cores halted during sleep; I2C/LEDC state preserved on wake
              if (axs_poll_touch()) {
                  display_backlight_set(100);
                  s_last_touch_ms = millis();
                  s_state = ACTIVE;
                  return;
              }
          }
      }
  }

  #endif // BATTERY_MODE
  ```

- [ ] **Step 3: Build default env — verifies new files compile cleanly without -DBATTERY_MODE**

  ```bash
  /home/alun/.platformio/penv/bin/platformio run -e jc3248w535c 2>&1 | tail -5
  ```
  Expected: `SUCCESS` (power_mgr.cpp compiles to nothing without the flag)

- [ ] **Step 4: Commit**

  ```bash
  git add src/power_mgr.h src/power_mgr.cpp
  git commit -m "Add power_mgr module: dim/off/light-sleep state machine (#ifdef BATTERY_MODE)"
  ```

---

## Chunk 3: Wiring and build configuration

### Task 4: Wire power_mgr into ui_task.cpp

**Files:**
- Modify: `src/ui_task.cpp`

Add `#include "power_mgr.h"` and two calls under `#ifdef BATTERY_MODE`:
1. `power_mgr_init()` — after WiFi connects and `show_boot_menu()` is called (startup)
2. `power_mgr_tick()` — in the main loop, outside `lvgl_lock`, after the WiFi monitor block

- [ ] **Step 1: Add include**

  After `#include "display.h"` (line 18), add:
  ```cpp
  #include "power_mgr.h"
  ```

- [ ] **Step 2: Add power_mgr_init() call at startup**

  In `ui_task_fn()`, after `show_boot_menu();` (line 544), add:
  ```cpp
  #ifdef BATTERY_MODE
      power_mgr_init();
  #endif
  ```

- [ ] **Step 3: Add power_mgr_tick() call in main loop**

  In the main loop, after the closing `}` of the WiFi monitor block (after line 758, before `vTaskDelay`), add:
  ```cpp
  #ifdef BATTERY_MODE
          power_mgr_tick();
  #endif
  ```

- [ ] **Step 4: Build default env — verifies wiring compiles without BATTERY_MODE**

  ```bash
  /home/alun/.platformio/penv/bin/platformio run -e jc3248w535c 2>&1 | tail -5
  ```
  Expected: `SUCCESS`

- [ ] **Step 5: Commit**

  ```bash
  git add src/ui_task.cpp
  git commit -m "Wire power_mgr_init/tick into ui_task (#ifdef BATTERY_MODE)"
  ```

---

### Task 5: Add battery PlatformIO environment

**Files:**
- Modify: `platformio.ini`

Add an env that extends the existing `jc3248w535c` env and adds `-DBATTERY_MODE`.

- [ ] **Step 1: Append battery env to platformio.ini**

  Add at end of file:
  ```ini
  [env:jc3248w535c_battery]
  extends = jc3248w535c
  build_flags =
      ${jc3248w535c.build_flags}
      -DBATTERY_MODE
  ```

- [ ] **Step 2: Build battery env**

  ```bash
  /home/alun/.platformio/penv/bin/platformio run -e jc3248w535c_battery 2>&1 | tail -10
  ```
  Expected: `SUCCESS` with no errors or warnings about undefined symbols.

- [ ] **Step 3: Commit**

  ```bash
  git add platformio.ini
  git commit -m "Add jc3248w535c_battery PlatformIO env with -DBATTERY_MODE"
  ```

---

## Upload (manual step)

To flash the battery build:
```bash
/home/alun/.platformio/penv/bin/platformio run -e jc3248w535c_battery --target upload
```

The standard `jc3248w535c` env is unaffected and continues to build as before.
