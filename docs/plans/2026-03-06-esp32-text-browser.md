# ESP32 Text Browser Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build a minimal touch web browser on the ESP32-S3 4848S040 that fetches text-only pages via textise.net proxy, with LVGL UI, swipe navigation, and multi-AP WiFi.

**Architecture:** Two FreeRTOS tasks (Network on core 0, UI on core 1) communicate via a queue. Network task fetches HTML from textise.net into a 512KB PSRAM buffer, runs the single-pass HTML tokenizer, then posts parsed PageElements to the UI task which builds LVGL widgets. LVGL runs its timer handler exclusively on core 1.

**Tech Stack:** PlatformIO + Arduino (ESP32 core 3.x), LVGL 8.3.x, esp_lcd RGB panel driver, bb_captouch (GT911), WiFiManager, HTTPClient + WiFiClientSecure, Preferences (NVS), FreeRTOS.

**Verification approach:** This is embedded firmware — "tests" are compile + flash + observe on device. Serial output at 115200 baud is used for verification at each step.

---

## Project Structure

```
browser/
├── platformio.ini
├── partitions_16MB.csv
├── lv_conf.h
├── src/
│   ├── main.cpp
│   ├── display.h / display.cpp       # ST7701S RGB panel + LVGL flush
│   ├── touch.h / touch.cpp           # GT911 + LVGL indev
│   ├── ui_task.h / ui_task.cpp       # LVGL FreeRTOS task (core 1)
│   ├── net_task.h / net_task.cpp     # Network FreeRTOS task (core 0)
│   ├── wifi_mgr.h / wifi_mgr.cpp     # Multi-AP NVS storage + connect
│   ├── fetcher.h / fetcher.cpp       # HTTPS fetch into PSRAM buffer
│   ├── html_parser.h / html_parser.cpp  # Single-pass tokenizer
│   ├── url_utils.h / url_utils.cpp   # URL resolution + textise wrapping
│   ├── history.h / history.cpp       # 50-URL navigation history
│   ├── page_renderer.h / page_renderer.cpp  # LVGL widget builder
│   ├── ui_header.h / ui_header.cpp   # Header bar (back/fwd/URL)
│   └── gesture.h / gesture.cpp      # Swipe detection
└── docs/plans/
    ├── 2026-03-06-esp32-text-browser-design.md
    └── 2026-03-06-esp32-text-browser.md  (this file)
```

---

### Task 1: PlatformIO Project Scaffold

**Files:**
- Create: `platformio.ini`
- Create: `partitions_16MB.csv`
- Create: `lv_conf.h`
- Create: `src/main.cpp`

**Step 1: Create `platformio.ini`**

```ini
[env:esp32-4848s0]
platform = espressif32
board = esp32s3box
framework = arduino
board_build.mcu = esp32s3
board_build.f_cpu = 240000000L
board_build.psram_type = opi
board_build.flash_mode = qio
board_build.flash_size = 16MB
board_upload.flash_size = 16MB
board_build.partitions = partitions_16MB.csv
board_build.arduino.memory_type = qio_opi

lib_deps =
    lvgl/lvgl @ ^8.3.11
    tzapu/WiFiManager @ ^2.0.17
    bitbank2/bb_captouch

build_flags =
    -DBOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DLV_CONF_INCLUDE_SIMPLE
    -Isrc

monitor_speed = 115200
monitor_filters = esp32_exception_decoder
```

**Step 2: Create `partitions_16MB.csv`**

```csv
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x5000,
otadata,  data, ota,     0xe000,   0x2000,
app0,     app,  ota_0,   0x10000,  0x640000,
app1,     app,  ota_1,   0x650000, 0x640000,
spiffs,   data, spiffs,  0xC90000, 0x360000,
coredump, data, coredump,0xFF0000, 0x10000,
```

**Step 3: Create `lv_conf.h`**

```c
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_HOR_RES_MAX  480
#define LV_VER_RES_MAX  480
#define LV_COLOR_DEPTH  16

/* PSRAM custom allocator */
#define LV_MEM_CUSTOM   1
#define LV_MEM_CUSTOM_INCLUDE  <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC    heap_caps_malloc_prefer
#define LV_MEM_CUSTOM_FREE     free
#define LV_MEM_CUSTOM_REALLOC  heap_caps_realloc_prefer

/* Enable logging */
#define LV_USE_LOG      1
#define LV_LOG_LEVEL    LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF   1

/* Tick */
#define LV_TICK_CUSTOM  1
#define LV_TICK_CUSTOM_INCLUDE  <Arduino.h>
#define LV_TICK_CUSTOM_SYS_TIME_EXPR  (millis())

/* Built-in fonts — we need multiple sizes */
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_18  1
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_MONTSERRAT_24  1
#define LV_FONT_DEFAULT        &lv_font_montserrat_14

/* Widgets needed */
#define LV_USE_LABEL    1
#define LV_USE_BTN      1
#define LV_USE_TEXTAREA 1
#define LV_USE_KEYBOARD 1
#define LV_USE_SPINNER  1
#define LV_USE_CONT     1
#define LV_USE_PAGE     1
#define LV_USE_MSGBOX   1

/* Animation for keyboard slide */
#define LV_USE_ANIMATION  1

/* Input device */
#define LV_USE_INDEV_TOUCHPAD  1

#endif /* LV_CONF_H */
```

**Step 4: Create `src/main.cpp` (skeleton)**

```cpp
// 060326 Initial scaffold
#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    Serial.println("ESP32 Text Browser starting...");
}

void loop() {
    delay(1000);
}
```

**Step 5: Compile**

```bash
cd /home/alun/esp32/browser
pio run
```

Expected: `SUCCESS` with no errors. Fix any compilation errors before continuing.

**Step 6: Commit**

```bash
git init
git add platformio.ini partitions_16MB.csv lv_conf.h src/main.cpp docs/
git commit -m "feat: initial project scaffold"
```

---

### Task 2: Display Driver (ST7701S via esp_lcd)

**Files:**
- Create: `src/display.h`
- Create: `src/display.cpp`

**Step 1: Create `src/display.h`**

```c
// 060326 ST7701S RGB panel driver with LVGL flush callback
#pragma once
#include <lvgl.h>

void display_init();
// Called after display_init() — registers LVGL display driver
void display_lvgl_init();
```

**Step 2: Create `src/display.cpp`**

```cpp
// 060326 ST7701S: SPI init commands + RGB panel via esp_lcd
#include "display.h"
#include <Arduino.h>
#include <SPI.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_panel_ops.h>

// ---- Pin definitions ----
#define LCD_BL      38
#define LCD_SPI_CS  39
#define LCD_SPI_SCK 48
#define LCD_SPI_MOSI 47

#define LCD_DE    18
#define LCD_VSYNC 17
#define LCD_HSYNC 16
#define LCD_PCLK  21

// RGB data pins: R[4:0], G[5:0], B[4:0]
static const int LCD_R[] = {4, 5, 6, 7, 15};
static const int LCD_G[] = {8, 20, 3, 46, 9, 10};
static const int LCD_B[] = {11, 12, 13, 14, 0};

#define LCD_H_RES 480
#define LCD_V_RES 480

// LVGL draw buffers — allocate in PSRAM
#define BUF_LINES 20
static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;

static esp_lcd_panel_handle_t panel_handle = nullptr;

// ---- ST7701S SPI init sequence ----
// Send a command byte via software SPI
static void st7701_spi_write(uint8_t cmd, const uint8_t *data, size_t len) {
    digitalWrite(LCD_SPI_CS, LOW);
    // Command byte: DC=0 => first bit 0, then 8-bit cmd
    // ST7701S uses 9-bit SPI: first bit = DC (0=cmd,1=data)
    uint16_t word = (0 << 8) | cmd;
    for (int i = 8; i >= 0; i--) {
        digitalWrite(LCD_SPI_SCK, LOW);
        digitalWrite(LCD_SPI_MOSI, (word >> i) & 1);
        digitalWrite(LCD_SPI_SCK, HIGH);
    }
    for (size_t b = 0; b < len; b++) {
        word = (1 << 8) | data[b];
        for (int i = 8; i >= 0; i--) {
            digitalWrite(LCD_SPI_SCK, LOW);
            digitalWrite(LCD_SPI_MOSI, (word >> i) & 1);
            digitalWrite(LCD_SPI_SCK, HIGH);
        }
    }
    digitalWrite(LCD_SPI_CS, HIGH);
}

static void st7701_init_sequence() {
    // Standard ST7701S init for 480x480
    // Page 0
    uint8_t d[16];

    // CND2BKxSEL: switch to page 1
    d[0]=0x77; d[1]=0x01; d[2]=0x00; d[3]=0x00; d[4]=0x10;
    st7701_spi_write(0xFF, d, 5);

    d[0]=0xE0; st7701_spi_write(0xC0, d, 1); // LNESET
    d[0]=0x03; st7701_spi_write(0xC1, d, 1);
    d[0]=0x02; d[1]=0x02; st7701_spi_write(0xC2, d, 2);
    // VGHS, VGLS
    d[0]=0xBB; st7701_spi_write(0xCC, d, 1);
    // Gamma B0
    uint8_t gamma_pos[] = {0x00,0x0D,0x14,0x0D,0x10,0x05,0x02,0x08,
                           0x08,0x1E,0x05,0x13,0x11,0x28,0x32,0x1C};
    st7701_spi_write(0xB0, gamma_pos, 16);
    // Gamma B1
    uint8_t gamma_neg[] = {0x00,0x0D,0x14,0x0C,0x10,0x05,0x02,0x08,
                           0x07,0x1E,0x04,0x13,0x11,0x28,0x32,0x1C};
    st7701_spi_write(0xB1, gamma_neg, 16);

    // Back to page 0
    d[0]=0x77; d[1]=0x01; d[2]=0x00; d[3]=0x00; d[4]=0x00;
    st7701_spi_write(0xFF, d, 5);

    // COLMOD: 16bpp RGB565
    d[0]=0x55; st7701_spi_write(0x3A, d, 1);
    // MADCTL
    d[0]=0x00; st7701_spi_write(0x36, d, 1);

    // Sleep out
    st7701_spi_write(0x11, nullptr, 0);
    delay(120);
    // Display on
    st7701_spi_write(0x29, nullptr, 0);
    delay(20);
}

// ---- LVGL flush callback ----
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map) {
    esp_lcd_panel_draw_bitmap(panel_handle,
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1,
                              color_map);
    lv_disp_flush_ready(drv);
}

void display_init() {
    // Backlight off during init
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, LOW);

    // SPI pins for ST7701S init
    pinMode(LCD_SPI_CS, OUTPUT);
    pinMode(LCD_SPI_SCK, OUTPUT);
    pinMode(LCD_SPI_MOSI, OUTPUT);
    digitalWrite(LCD_SPI_CS, HIGH);
    digitalWrite(LCD_SPI_SCK, LOW);

    // Run ST7701S init sequence
    st7701_init_sequence();

    // Configure RGB panel
    esp_lcd_rgb_panel_config_t panel_config = {};
    panel_config.clk_src = LCD_CLK_SRC_DEFAULT;
    panel_config.timings.h_res = LCD_H_RES;
    panel_config.timings.v_res = LCD_V_RES;
    panel_config.timings.pixel_clock_hz = 16 * 1000 * 1000; // 16MHz
    panel_config.timings.hsync_back_porch = 10;
    panel_config.timings.hsync_front_porch = 50;
    panel_config.timings.hsync_pulse_width = 8;
    panel_config.timings.vsync_back_porch = 10;
    panel_config.timings.vsync_front_porch = 20;
    panel_config.timings.vsync_pulse_width = 8;
    panel_config.timings.flags.pclk_active_neg = 1;
    panel_config.data_width = 16; // RGB565
    panel_config.num_fbs = 1;
    panel_config.psram_trans_align = 64;

    // RGB data pins flat array: R4..R0, G5..G0, B4..B0
    int data_pins[] = {LCD_R[4],LCD_R[3],LCD_R[2],LCD_R[1],LCD_R[0],
                       LCD_G[5],LCD_G[4],LCD_G[3],LCD_G[2],LCD_G[1],LCD_G[0],
                       LCD_B[4],LCD_B[3],LCD_B[2],LCD_B[1],LCD_B[0]};
    memcpy(panel_config.data_gpio_nums, data_pins, sizeof(data_pins));

    panel_config.hsync_gpio_num = LCD_HSYNC;
    panel_config.vsync_gpio_num = LCD_VSYNC;
    panel_config.de_gpio_num    = LCD_DE;
    panel_config.pclk_gpio_num  = LCD_PCLK;
    panel_config.disp_gpio_num  = GPIO_NUM_NC;
    panel_config.flags.fb_in_psram = 1;

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    // Backlight on
    digitalWrite(LCD_BL, HIGH);
    Serial.println("Display initialised");
}

void display_lvgl_init() {
    // Allocate LVGL draw buffers in PSRAM
    size_t buf_size = LCD_H_RES * BUF_LINES * sizeof(lv_color_t);
    buf1 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    buf2 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    assert(buf1 && buf2);

    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LCD_H_RES * BUF_LINES);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = LCD_H_RES;
    disp_drv.ver_res  = LCD_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
    Serial.println("LVGL display driver registered");
}
```

**Step 3: Update `src/main.cpp` to test display**

```cpp
#include <Arduino.h>
#include <lvgl.h>
#include "display.h"

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("Boot");

    lv_init();
    display_init();
    display_lvgl_init();

    // Test: fill screen blue
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0000FF), 0);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Display OK");
    lv_obj_center(label);
}

void loop() {
    lv_timer_handler();
    delay(5);
}
```

**Step 4: Compile and flash**

```bash
pio run --target upload
pio device monitor
```

Expected: Screen lights up blue with "Display OK" text centred. Serial shows "Display initialised" and "LVGL display driver registered".

If screen is blank: check backlight pin, SPI init sequence. If garbled: check RGB pin order (may need to swap R/B arrays for your specific board revision).

**Step 5: Commit**

```bash
git add src/display.h src/display.cpp src/main.cpp
git commit -m "feat: ST7701S RGB panel driver with LVGL flush callback"
```

---

### Task 3: Touch Driver (GT911 via bb_captouch)

**Files:**
- Create: `src/touch.h`
- Create: `src/touch.cpp`

**Step 1: Create `src/touch.h`**

```c
// 060326 GT911 touch via bb_captouch, registered as LVGL indev
#pragma once
#include <lvgl.h>

void touch_init();
void touch_lvgl_init();
```

**Step 2: Create `src/touch.cpp`**

```cpp
// 060326 GT911 on I2C SDA=19 SCL=45, addr auto-detected by bb_captouch
#include "touch.h"
#include <Arduino.h>
#include <bb_captouch.h>

#define TOUCH_SDA 19
#define TOUCH_SCL 45

static BBCapTouch touch;
static lv_indev_drv_t indev_drv;

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    TOUCHINFO ti;
    if (touch.getSamples(&ti) && ti.count > 0) {
        data->point.x = ti.x[0];
        data->point.y = ti.y[0];
        data->state   = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

void touch_init() {
    if (!touch.init(TOUCH_SDA, TOUCH_SCL, TOUCH_RST_NOT_USED, TOUCH_INT_NOT_USED)) {
        Serial.println("ERROR: Touch init failed");
        return;
    }
    Serial.println("Touch initialised (GT911)");
}

void touch_lvgl_init() {
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&indev_drv);
    Serial.println("LVGL touch indev registered");
}
```

**Step 3: Update `src/main.cpp` to test touch**

```cpp
#include <Arduino.h>
#include <lvgl.h>
#include "display.h"
#include "touch.h"

static void btn_cb(lv_event_t *e) {
    Serial.println("Button tapped!");
}

void setup() {
    Serial.begin(115200);
    delay(500);

    lv_init();
    display_init();
    display_lvgl_init();
    touch_init();
    touch_lvgl_init();

    lv_obj_t *btn = lv_btn_create(lv_scr_act());
    lv_obj_center(btn);
    lv_obj_set_size(btn, 200, 80);
    lv_obj_add_event_cb(btn, btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Tap me");
    lv_obj_center(lbl);
}

void loop() {
    lv_timer_handler();
    delay(5);
}
```

**Step 4: Compile, flash, verify**

```bash
pio run --target upload
pio device monitor
```

Expected: Button on screen. Tapping it prints "Button tapped!" to serial. If touch coordinates are mirrored, add coordinate inversion in `touch_read_cb`:
```c
data->point.x = 480 - ti.x[0];  // mirror X if needed
data->point.y = 480 - ti.y[0];  // mirror Y if needed
```

**Step 5: Commit**

```bash
git add src/touch.h src/touch.cpp src/main.cpp
git commit -m "feat: GT911 touch driver registered as LVGL indev"
```

---

### Task 4: FreeRTOS UI Task

**Files:**
- Create: `src/ui_task.h`
- Create: `src/ui_task.cpp`

**Step 1: Create `src/ui_task.h`**

```c
// 060326 LVGL UI task pinned to core 1
#pragma once

void ui_task_start();
// Called from UI task context to build the root screen
void ui_build_root();
```

**Step 2: Create `src/ui_task.cpp`**

```cpp
// 060326 FreeRTOS task: runs lv_timer_handler on core 1
#include "ui_task.h"
#include <Arduino.h>
#include <lvgl.h>
#include "display.h"
#include "touch.h"

static SemaphoreHandle_t lvgl_mutex;

// Acquire mutex before touching LVGL from other tasks
bool lvgl_lock(uint32_t timeout_ms) {
    return xSemaphoreTakeRecursive(lvgl_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}
void lvgl_unlock() {
    xSemaphoreGiveRecursive(lvgl_mutex);
}

static void ui_task_fn(void *arg) {
    lv_init();
    display_init();
    display_lvgl_init();
    touch_init();
    touch_lvgl_init();

    ui_build_root();

    for (;;) {
        if (lvgl_lock(10)) {
            lv_timer_handler();
            lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void ui_task_start() {
    lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    xTaskCreatePinnedToCore(ui_task_fn, "ui_task", 8192, nullptr, 5, nullptr, 1);
}

void ui_build_root() {
    // Placeholder — Task 12 replaces this
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "UI Task Running");
    lv_obj_center(label);
}
```

**Step 3: Update `src/main.cpp`**

```cpp
#include <Arduino.h>
#include "ui_task.h"

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("Starting UI task...");
    ui_task_start();
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
```

**Step 4: Compile, flash, verify**

```bash
pio run --target upload
pio device monitor
```

Expected: "UI Task Running" displayed. Serial shows no stack overflow messages. Check free heap with `Serial.println(ESP.getFreeHeap())` if needed.

**Step 5: Commit**

```bash
git add src/ui_task.h src/ui_task.cpp src/main.cpp
git commit -m "feat: LVGL UI FreeRTOS task pinned to core 1"
```

---

### Task 5: WiFi Multi-AP Manager

**Files:**
- Create: `src/wifi_mgr.h`
- Create: `src/wifi_mgr.cpp`

**Step 1: Create `src/wifi_mgr.h`**

```c
// 060326 Multi-AP WiFi manager: 10 slots in NVS, scan-and-match
#pragma once
#include <stdbool.h>

#define WIFI_MAX_APS    10
#define WIFI_SSID_LEN   64
#define WIFI_PASS_LEN   64
#define WIFI_CONNECT_TIMEOUT_MS 10000

void wifi_mgr_init();            // load APs from NVS
bool wifi_mgr_connect();         // scan + connect to first known AP
void wifi_mgr_add_ap(const char *ssid, const char *pass); // save new AP
void wifi_mgr_start_portal();    // launch captive portal (blocks until configured)
bool wifi_mgr_is_connected();
```

**Step 2: Create `src/wifi_mgr.cpp`**

```cpp
// 060326 NVS-backed multi-AP WiFi with scan-and-match
#include "wifi_mgr.h"
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <WiFiManager.h>

static Preferences prefs;
static char ssids[WIFI_MAX_APS][WIFI_SSID_LEN];
static char passes[WIFI_MAX_APS][WIFI_PASS_LEN];
static int  ap_count = 0;

static const char *NVS_NS   = "wifimgr";
static const char *NVS_CNT  = "count";

static String ssid_key(int i) { return "ssid" + String(i); }
static String pass_key(int i) { return "pass" + String(i); }

void wifi_mgr_init() {
    prefs.begin(NVS_NS, false);
    ap_count = prefs.getInt(NVS_CNT, 0);
    for (int i = 0; i < ap_count; i++) {
        String s = prefs.getString(ssid_key(i).c_str(), "");
        String p = prefs.getString(pass_key(i).c_str(), "");
        strncpy(ssids[i], s.c_str(), WIFI_SSID_LEN - 1);
        strncpy(passes[i], p.c_str(), WIFI_PASS_LEN - 1);
    }
    prefs.end();
    Serial.printf("WiFi: loaded %d saved APs\n", ap_count);
}

bool wifi_mgr_connect() {
    if (ap_count == 0) return false;

    Serial.println("WiFi: scanning...");
    int n = WiFi.scanNetworks();
    Serial.printf("WiFi: found %d networks\n", n);

    for (int s = 0; s < n; s++) {
        String found_ssid = WiFi.SSID(s);
        for (int k = 0; k < ap_count; k++) {
            if (found_ssid == ssids[k]) {
                Serial.printf("WiFi: matching AP '%s', connecting...\n", ssids[k]);
                WiFi.begin(ssids[k], passes[k]);
                uint32_t t = millis();
                while (WiFi.status() != WL_CONNECTED) {
                    if (millis() - t > WIFI_CONNECT_TIMEOUT_MS) {
                        Serial.println("WiFi: connect timeout");
                        WiFi.disconnect();
                        break;
                    }
                    delay(100);
                }
                if (WiFi.status() == WL_CONNECTED) {
                    Serial.printf("WiFi: connected, IP=%s\n",
                                  WiFi.localIP().toString().c_str());
                    WiFi.scanDelete();
                    return true;
                }
            }
        }
    }
    WiFi.scanDelete();
    return false;
}

void wifi_mgr_add_ap(const char *ssid, const char *pass) {
    // Check if SSID already exists — update password
    for (int i = 0; i < ap_count; i++) {
        if (strcmp(ssids[i], ssid) == 0) {
            strncpy(passes[i], pass, WIFI_PASS_LEN - 1);
            prefs.begin(NVS_NS, false);
            prefs.putString(pass_key(i).c_str(), pass);
            prefs.end();
            Serial.printf("WiFi: updated password for '%s'\n", ssid);
            return;
        }
    }
    // New AP — evict oldest if full
    if (ap_count >= WIFI_MAX_APS) {
        // Shift left (evict index 0)
        for (int i = 0; i < WIFI_MAX_APS - 1; i++) {
            memcpy(ssids[i], ssids[i+1], WIFI_SSID_LEN);
            memcpy(passes[i], passes[i+1], WIFI_PASS_LEN);
        }
        ap_count = WIFI_MAX_APS - 1;
    }
    strncpy(ssids[ap_count], ssid, WIFI_SSID_LEN - 1);
    strncpy(passes[ap_count], pass, WIFI_PASS_LEN - 1);
    ap_count++;

    prefs.begin(NVS_NS, false);
    prefs.putInt(NVS_CNT, ap_count);
    for (int i = 0; i < ap_count; i++) {
        prefs.putString(ssid_key(i).c_str(), ssids[i]);
        prefs.putString(pass_key(i).c_str(), passes[i]);
    }
    prefs.end();
    Serial.printf("WiFi: saved AP '%s' (total: %d)\n", ssid, ap_count);
}

void wifi_mgr_start_portal() {
    Serial.println("WiFi: starting captive portal 'ESP32-Browser'");
    WiFiManager wm;
    wm.setSaveConfigCallback([]() {});
    // Non-blocking config portal with auto-connect off
    wm.setConfigPortalTimeout(0); // no timeout
    bool ok = wm.startConfigPortal("ESP32-Browser");
    if (ok) {
        // WiFiManager has connected — save credentials to our NVS list
        wifi_mgr_add_ap(wm.getWiFiSSID().c_str(), wm.getWiFiPass().c_str());
        Serial.printf("WiFi: portal configured SSID='%s'\n",
                      wm.getWiFiSSID().c_str());
    }
}

bool wifi_mgr_is_connected() {
    return WiFi.status() == WL_CONNECTED;
}
```

**Step 3: Update `src/main.cpp` to test WiFi**

```cpp
#include <Arduino.h>
#include "wifi_mgr.h"

void setup() {
    Serial.begin(115200);
    delay(500);

    wifi_mgr_init();

    if (!wifi_mgr_connect()) {
        Serial.println("No known AP found, starting portal...");
        wifi_mgr_start_portal();
    }

    if (wifi_mgr_is_connected()) {
        Serial.println("WiFi OK — ready");
    }
}

void loop() { delay(1000); }
```

**Step 4: Compile, flash, verify**

```bash
pio run --target upload
pio device monitor
```

Expected on first boot: "WiFi: loaded 0 saved APs", "No known AP found, starting portal...". Connect phone to "ESP32-Browser" AP, configure WiFi. Serial shows "WiFi: portal configured SSID='YourNetwork'". On next boot: "WiFi: loaded 1 saved APs", connects automatically.

**Step 5: Commit**

```bash
git add src/wifi_mgr.h src/wifi_mgr.cpp src/main.cpp
git commit -m "feat: multi-AP WiFi manager with NVS storage and captive portal"
```

---

### Task 6: HTTP Fetcher (HTTPS into PSRAM)

**Files:**
- Create: `src/fetcher.h`
- Create: `src/fetcher.cpp`

**Step 1: Create `src/fetcher.h`**

```c
// 060326 HTTPS page fetcher into PSRAM buffer
#pragma once
#include <stddef.h>

#define FETCH_BUF_SIZE (512 * 1024)  // 512KB

// fetch_page: fetches textise.net proxy URL for `url` into PSRAM buffer.
// Returns number of bytes fetched, or -1 on error.
// Buffer is null-terminated.
// Caller must not free buf — it is a static PSRAM allocation.
int fetch_page(const char *url, char **buf_out);
```

**Step 2: Create `src/fetcher.cpp`**

```cpp
// 060326 Fetch text via textise.net proxy into PSRAM
#include "fetcher.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

static char *fetch_buf = nullptr;

static void ensure_buf() {
    if (!fetch_buf) {
        fetch_buf = (char *)heap_caps_malloc(FETCH_BUF_SIZE, MALLOC_CAP_SPIRAM);
        assert(fetch_buf);
    }
}

// Wrap any URL through textise.net
static String make_textise_url(const char *url) {
    return String("https://www.textise.net/showText.aspx?strURL=") + url;
}

int fetch_page(const char *url, char **buf_out) {
    ensure_buf();
    *buf_out = fetch_buf;

    String proxy_url = make_textise_url(url);
    Serial.printf("Fetching: %s\n", proxy_url.c_str());

    WiFiClientSecure client;
    client.setInsecure(); // textise.net cert not pinned
    client.setTimeout(15);

    HTTPClient http;
    http.begin(client, proxy_url);
    http.addHeader("User-Agent", "ESP32Browser/1.0");
    http.addHeader("Accept", "text/html");

    int code = http.GET();
    if (code != 200) {
        Serial.printf("HTTP error: %d\n", code);
        http.end();
        return -1;
    }

    int len = http.getSize();
    Serial.printf("Content-Length: %d\n", len);

    WiFiClient *stream = http.getStreamPtr();
    size_t total = 0;
    size_t cap   = FETCH_BUF_SIZE - 1;

    while (http.connected() && total < cap) {
        size_t avail = stream->available();
        if (avail) {
            size_t to_read = min(avail, cap - total);
            total += stream->readBytes(fetch_buf + total, to_read);
        } else {
            delay(1);
        }
    }
    fetch_buf[total] = '\0';
    http.end();

    Serial.printf("Fetched %zu bytes\n", total);
    return (int)total;
}
```

**Step 3: Update `src/main.cpp` to test fetcher**

```cpp
#include <Arduino.h>
#include "wifi_mgr.h"
#include "fetcher.h"

void setup() {
    Serial.begin(115200);
    delay(500);

    wifi_mgr_init();
    if (!wifi_mgr_connect()) {
        wifi_mgr_start_portal();
    }

    char *buf;
    int n = fetch_page("https://en.wikipedia.org/wiki/ESP32", &buf);
    if (n > 0) {
        Serial.printf("Fetch OK: %d bytes\n", n);
        // Print first 500 chars
        Serial.write(buf, min(n, 500));
        Serial.println();
    } else {
        Serial.println("Fetch failed");
    }
}

void loop() { delay(1000); }
```

**Step 4: Compile, flash, verify**

```bash
pio run --target upload
pio device monitor
```

Expected: "Fetched NNNN bytes", followed by first 500 chars of textise.net output (should look like stripped HTML text about ESP32).

**Step 5: Commit**

```bash
git add src/fetcher.h src/fetcher.cpp src/main.cpp
git commit -m "feat: HTTPS page fetcher via textise.net proxy into PSRAM"
```

---

### Task 7: URL Utilities (resolution + textise wrapping)

**Files:**
- Create: `src/url_utils.h`
- Create: `src/url_utils.cpp`

**Step 1: Create `src/url_utils.h`**

```c
// 060326 URL resolution: relative -> absolute, textise.net wrapping
#pragma once

// Resolve href relative to base_url. Result written into out (max out_len).
// Returns out on success, nullptr on failure.
char *url_resolve(const char *base_url, const char *href,
                  char *out, size_t out_len);

// Wrap a fully-resolved URL through textise.net proxy.
// Result written into out.
char *url_textise(const char *url, char *out, size_t out_len);

// Update the base URL from a newly navigated URL (extracts scheme+host+dir)
void url_set_base(const char *navigated_url);
const char *url_get_base();
```

**Step 2: Create `src/url_utils.cpp`**

```cpp
// 060326 URL resolution logic
#include "url_utils.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

static char base_url[512] = "";

void url_set_base(const char *navigated_url) {
    // Extract scheme://host/path/  (drop filename)
    strncpy(base_url, navigated_url, sizeof(base_url) - 1);
    // Find last '/' after scheme://
    const char *scheme_end = strstr(base_url, "://");
    if (!scheme_end) { base_url[0] = '\0'; return; }
    char *last_slash = strrchr(scheme_end + 3, '/');
    if (last_slash) {
        *(last_slash + 1) = '\0'; // keep trailing slash
    }
}

const char *url_get_base() { return base_url; }

char *url_resolve(const char *base, const char *href, char *out, size_t out_len) {
    if (!href || !*href) return nullptr;

    // Already absolute
    if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0) {
        strncpy(out, href, out_len - 1);
        out[out_len - 1] = '\0';
        return out;
    }

    // Protocol-relative: //host/path
    if (strncmp(href, "//", 2) == 0) {
        // Grab scheme from base
        const char *colon = strchr(base, ':');
        if (!colon) return nullptr;
        size_t scheme_len = colon - base + 1;
        strncpy(out, base, scheme_len);
        out[scheme_len] = '\0';
        strncat(out, href, out_len - scheme_len - 1);
        return out;
    }

    // Root-relative: /path
    if (href[0] == '/') {
        // Grab scheme://host from base
        const char *scheme_end = strstr(base, "://");
        if (!scheme_end) return nullptr;
        const char *host_end = strchr(scheme_end + 3, '/');
        size_t prefix_len = host_end ? (size_t)(host_end - base)
                                     : strlen(base);
        strncpy(out, base, prefix_len);
        out[prefix_len] = '\0';
        strncat(out, href, out_len - prefix_len - 1);
        return out;
    }

    // Relative: append to base dir
    snprintf(out, out_len, "%s%s", base, href);
    return out;
}

char *url_textise(const char *url, char *out, size_t out_len) {
    snprintf(out, out_len,
             "https://www.textise.net/showText.aspx?strURL=%s", url);
    return out;
}
```

**Step 3: Test via serial (add to main.cpp)**

```cpp
// Add to setup() after WiFi:
char resolved[512];
const char *base = "https://en.wikipedia.org/wiki/ESP32";
url_set_base(base);
Serial.println(url_get_base()); // "https://en.wikipedia.org/wiki/"

url_resolve(url_get_base(), "/wiki/Arduino", resolved, sizeof(resolved));
Serial.println(resolved); // "https://en.wikipedia.org/wiki/Arduino"

url_resolve(url_get_base(), "Microcontroller", resolved, sizeof(resolved));
Serial.println(resolved); // "https://en.wikipedia.org/wiki/Microcontroller"

url_resolve(url_get_base(), "https://other.com/page", resolved, sizeof(resolved));
Serial.println(resolved); // "https://other.com/page"
```

**Step 4: Compile, flash, verify serial output matches expected**

**Step 5: Commit**

```bash
git add src/url_utils.h src/url_utils.cpp src/main.cpp
git commit -m "feat: URL resolution and textise.net wrapping utilities"
```

---

### Task 8: HTML Tokenizer

**Files:**
- Create: `src/html_parser.h`
- Create: `src/html_parser.cpp`

**Step 1: Create `src/html_parser.h`**

```c
// 060326 Single-pass HTML tokenizer -> PageElement array in PSRAM
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ELEM_HEADING,
    ELEM_PARAGRAPH,
    ELEM_LINK,
    ELEM_LINEBREAK
} ElemType;

typedef struct {
    ElemType type;
    char    *text;    // in PSRAM text pool
    char    *href;    // resolved absolute URL (ELEM_LINK only), or NULL
    uint8_t  level;   // 1-6 for ELEM_HEADING, 0 otherwise
} PageElement;

#define MAX_ELEMENTS 512

typedef struct {
    PageElement elems[MAX_ELEMENTS];
    int         count;
    char        text_pool[65536]; // 64KB inline text pool (in PSRAM struct)
    size_t      pool_used;
} ParseResult;

// Allocate ParseResult in PSRAM
ParseResult *parse_result_alloc();
void         parse_result_free(ParseResult *r);

// Parse html_buf (null-terminated) into r.
// base_url used for link resolution.
void html_parse(const char *html_buf, const char *base_url, ParseResult *r);
```

**Step 2: Create `src/html_parser.cpp`**

```cpp
// 060326 Single-pass HTML tokenizer
#include "html_parser.h"
#include "url_utils.h"
#include <Arduino.h>
#include <string.h>
#include <ctype.h>

ParseResult *parse_result_alloc() {
    ParseResult *r = (ParseResult *)heap_caps_malloc(sizeof(ParseResult),
                                                      MALLOC_CAP_SPIRAM);
    if (r) { memset(r, 0, sizeof(ParseResult)); }
    return r;
}

void parse_result_free(ParseResult *r) {
    if (r) heap_caps_free(r);
}

// Add text to pool, return pointer. Returns NULL if pool full.
static char *pool_add(ParseResult *r, const char *text, size_t len) {
    if (r->pool_used + len + 1 > sizeof(r->text_pool)) return NULL;
    char *p = r->text_pool + r->pool_used;
    memcpy(p, text, len);
    p[len] = '\0';
    r->pool_used += len + 1;
    return p;
}

// Append element. Returns false if full.
static bool add_elem(ParseResult *r, ElemType type, const char *text,
                     size_t text_len, const char *href, uint8_t level) {
    if (r->count >= MAX_ELEMENTS) return false;
    // Skip empty text (except linebreak)
    if (type != ELEM_LINEBREAK && text_len == 0) return true;

    PageElement *e = &r->elems[r->count];
    e->type  = type;
    e->level = level;
    e->text  = pool_add(r, text, text_len);
    e->href  = NULL;

    if (type == ELEM_LINK && href) {
        e->href = pool_add(r, href, strlen(href));
    }
    if (e->text || type == ELEM_LINEBREAK) r->count++;
    return true;
}

// Case-insensitive tag name match
static bool tag_is(const char *tag_start, size_t tag_len, const char *name) {
    size_t name_len = strlen(name);
    if (tag_len < name_len) return false;
    return strncasecmp(tag_start, name, name_len) == 0 &&
           (tag_len == name_len || !isalnum((uint8_t)tag_start[name_len]));
}

// Extract attribute value from tag string. out must be >= out_len.
static bool get_attr(const char *tag, const char *attr, char *out, size_t out_len) {
    const char *p = strcasestr(tag, attr);
    if (!p) return false;
    p += strlen(attr);
    while (*p == ' ') p++;
    if (*p != '=') return false;
    p++;
    while (*p == ' ') p++;
    char quote = 0;
    if (*p == '"' || *p == '\'') { quote = *p++; }
    size_t i = 0;
    while (*p && i < out_len - 1) {
        if (quote && *p == quote) break;
        if (!quote && (*p == ' ' || *p == '>')) break;
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

// Decode common HTML entities in-place (simple subset)
static void decode_entities(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (*r == '&') {
            if (strncmp(r, "&amp;", 5) == 0)  { *w++ = '&';  r += 5; }
            else if (strncmp(r, "&lt;",  4) == 0) { *w++ = '<';  r += 4; }
            else if (strncmp(r, "&gt;",  4) == 0) { *w++ = '>';  r += 4; }
            else if (strncmp(r, "&nbsp;",6) == 0) { *w++ = ' ';  r += 6; }
            else if (strncmp(r, "&quot;",6) == 0) { *w++ = '"';  r += 6; }
            else if (strncmp(r, "&#39;", 5) == 0) { *w++ = '\''; r += 5; }
            else { *w++ = *r++; }
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

void html_parse(const char *html, const char *base_url, ParseResult *r) {
    url_set_base(base_url);

    // Current paragraph/text accumulator
    static char text_acc[4096];
    size_t acc_len = 0;

    // Current context
    ElemType cur_type  = ELEM_PARAGRAPH;
    uint8_t  cur_level = 0;
    bool     in_body   = false;
    bool     in_link   = false;
    char     link_href[512] = "";

    const char *p = html;

    auto flush_acc = [&]() {
        if (acc_len > 0) {
            text_acc[acc_len] = '\0';
            decode_entities(text_acc);
            // Trim whitespace
            size_t start = 0;
            while (start < acc_len && isspace((uint8_t)text_acc[start])) start++;
            size_t end = strlen(text_acc);
            while (end > start && isspace((uint8_t)text_acc[end-1])) end--;
            size_t len = end - start;
            if (len > 0) {
                add_elem(r, in_link ? ELEM_LINK : cur_type,
                         text_acc + start, len,
                         in_link ? link_href : NULL,
                         cur_level);
            }
            acc_len = 0;
        }
    };

    while (*p) {
        if (*p == '<') {
            // Find end of tag
            const char *tag_start = p + 1;
            const char *tag_end   = strchr(tag_start, '>');
            if (!tag_end) break;

            bool closing = (tag_start[0] == '/');
            const char *name = closing ? tag_start + 1 : tag_start;
            size_t name_len  = 0;
            while (name[name_len] && name[name_len] != ' ' &&
                   name[name_len] != '>' && name[name_len] != '/') name_len++;

            // Full tag string for attribute extraction
            size_t tag_str_len = tag_end - tag_start;
            static char tag_str[1024];
            size_t copy_len = min(tag_str_len, sizeof(tag_str) - 1);
            memcpy(tag_str, tag_start, copy_len);
            tag_str[copy_len] = '\0';

            // Skip <script>, <style> blocks
            if (!closing && (tag_is(name, name_len, "script") ||
                             tag_is(name, name_len, "style"))) {
                const char *close = strcasestr(tag_end, tag_is(name, name_len, "script")
                                               ? "</script>" : "</style>");
                if (close) p = close + 9;
                else p = tag_end + 1;
                continue;
            }

            if (!in_body) {
                if (tag_is(name, name_len, "body")) in_body = true;
                p = tag_end + 1;
                continue;
            }

            // Headings
            if (name_len == 2 && tolower(name[0]) == 'h' &&
                name[1] >= '1' && name[1] <= '6') {
                flush_acc();
                if (!closing) {
                    cur_type  = ELEM_HEADING;
                    cur_level = name[1] - '0';
                } else {
                    flush_acc();
                    cur_type  = ELEM_PARAGRAPH;
                    cur_level = 0;
                }
            }
            // Paragraphs
            else if (tag_is(name, name_len, "p")) {
                flush_acc();
                if (!closing) { cur_type = ELEM_PARAGRAPH; cur_level = 0; }
            }
            // Line break
            else if (tag_is(name, name_len, "br")) {
                flush_acc();
                add_elem(r, ELEM_LINEBREAK, "", 0, NULL, 0);
            }
            // Links
            else if (tag_is(name, name_len, "a")) {
                flush_acc();
                if (!closing) {
                    char href_raw[512] = "";
                    if (get_attr(tag_str, "href", href_raw, sizeof(href_raw))) {
                        char resolved[512];
                        if (url_resolve(url_get_base(), href_raw,
                                        resolved, sizeof(resolved))) {
                            strncpy(link_href, resolved, sizeof(link_href) - 1);
                            in_link = true;
                        }
                    }
                } else {
                    in_link = false;
                    link_href[0] = '\0';
                }
            }
            // Div/li/td/tr — treat as paragraph break
            else if (tag_is(name, name_len, "div") ||
                     tag_is(name, name_len, "li")  ||
                     tag_is(name, name_len, "td")  ||
                     tag_is(name, name_len, "tr")) {
                flush_acc();
            }

            p = tag_end + 1;
        } else {
            // Text content
            if (in_body && acc_len < sizeof(text_acc) - 1) {
                // Collapse whitespace
                if (isspace((uint8_t)*p)) {
                    if (acc_len > 0 && text_acc[acc_len-1] != ' ')
                        text_acc[acc_len++] = ' ';
                } else {
                    text_acc[acc_len++] = *p;
                }
            }
            p++;
        }
    }
    flush_acc();
    Serial.printf("HTML parse: %d elements, %zu bytes pool used\n",
                  r->count, r->pool_used);
}
```

**Step 3: Test via serial**

```cpp
// In setup() after WiFi + fetch:
char *buf;
int n = fetch_page("https://en.wikipedia.org/wiki/ESP32", &buf);
if (n > 0) {
    ParseResult *result = parse_result_alloc();
    html_parse(buf, "https://en.wikipedia.org/wiki/ESP32", result);
    Serial.printf("Elements: %d\n", result->count);
    for (int i = 0; i < min(result->count, 20); i++) {
        PageElement *e = &result->elems[i];
        switch (e->type) {
            case ELEM_HEADING:
                Serial.printf("H%d: %s\n", e->level, e->text);
                break;
            case ELEM_LINK:
                Serial.printf("LINK: %s -> %s\n", e->text, e->href);
                break;
            case ELEM_PARAGRAPH:
                Serial.printf("P: %.80s\n", e->text);
                break;
            case ELEM_LINEBREAK:
                Serial.println("<BR>");
                break;
        }
    }
    parse_result_free(result);
}
```

**Step 4: Compile, flash, verify serial shows parsed elements correctly**

**Step 5: Commit**

```bash
git add src/html_parser.h src/html_parser.cpp src/main.cpp
git commit -m "feat: single-pass HTML tokenizer with PSRAM PageElement pool"
```

---

### Task 9: Navigation History

**Files:**
- Create: `src/history.h`
- Create: `src/history.cpp`

**Step 1: Create `src/history.h`**

```c
// 060326 50-URL navigation history stack in PSRAM
#pragma once
#include <stdbool.h>

#define HISTORY_MAX    50
#define HISTORY_URL_LEN 512

void        history_init();
void        history_push(const char *url);
const char *history_back();     // returns URL or NULL
const char *history_forward();  // returns URL or NULL
const char *history_current();
bool        history_can_back();
bool        history_can_forward();
```

**Step 2: Create `src/history.cpp`**

```cpp
// 060326 PSRAM-backed navigation history
#include "history.h"
#include <Arduino.h>
#include <string.h>

// Allocate entire history in PSRAM
static char (*urls)[HISTORY_URL_LEN] = nullptr;
static int head    = -1; // index of most recent entry
static int current = -1; // index of current view

void history_init() {
    urls = (char (*)[HISTORY_URL_LEN])
        heap_caps_malloc(HISTORY_MAX * HISTORY_URL_LEN, MALLOC_CAP_SPIRAM);
    assert(urls);
    head = current = -1;
}

void history_push(const char *url) {
    // Clear forward history
    head = current;

    if (head < HISTORY_MAX - 1) {
        head++;
    } else {
        // Shift left to evict oldest
        memmove(urls[0], urls[1], (HISTORY_MAX - 1) * HISTORY_URL_LEN);
        // head stays at HISTORY_MAX - 1
    }
    strncpy(urls[head], url, HISTORY_URL_LEN - 1);
    urls[head][HISTORY_URL_LEN - 1] = '\0';
    current = head;
}

const char *history_current() {
    if (current < 0) return nullptr;
    return urls[current];
}

bool history_can_back()    { return current > 0; }
bool history_can_forward() { return current < head; }

const char *history_back() {
    if (!history_can_back()) return nullptr;
    current--;
    return urls[current];
}

const char *history_forward() {
    if (!history_can_forward()) return nullptr;
    current++;
    return urls[current];
}
```

**Step 3: Verify via serial**

```cpp
history_init();
history_push("https://a.com");
history_push("https://b.com");
history_push("https://c.com");
Serial.println(history_current()); // c.com
Serial.println(history_back());    // b.com
Serial.println(history_back());    // a.com
Serial.println(history_can_back() ? "can back" : "no back"); // no back
Serial.println(history_forward()); // b.com
```

**Step 4: Compile, flash, verify output**

**Step 5: Commit**

```bash
git add src/history.h src/history.cpp src/main.cpp
git commit -m "feat: 50-URL PSRAM navigation history with back/forward"
```

---

### Task 10: Page Renderer (LVGL widgets from PageElements)

**Files:**
- Create: `src/page_renderer.h`
- Create: `src/page_renderer.cpp`

**Step 1: Create `src/page_renderer.h`**

```c
// 060326 Build LVGL widgets from parsed PageElement list
#pragma once
#include <lvgl.h>
#include "html_parser.h"

// Callback invoked when a link is tapped. url is the resolved href.
typedef void (*link_tap_cb_t)(const char *url);

// Render page into parent container. Clears existing children first.
void page_render(lv_obj_t *container, const ParseResult *result,
                 link_tap_cb_t on_link_tap);

// Clear the page container (e.g., before showing spinner)
void page_clear(lv_obj_t *container);

// Show/hide loading spinner in container
void page_show_spinner(lv_obj_t *container);
```

**Step 2: Create `src/page_renderer.cpp`**

```cpp
// 060326 LVGL page renderer
#include "page_renderer.h"
#include <Arduino.h>

// Colour scheme
#define COLOUR_BG       lv_color_hex(0x1A1A2E)
#define COLOUR_TEXT     lv_color_hex(0xE0E0E0)
#define COLOUR_HEADING  lv_color_hex(0xFFFFFF)
#define COLOUR_LINK     lv_color_hex(0x4FC3F7)
#define COLOUR_LINK_BG  lv_color_hex(0x16213E)

static link_tap_cb_t s_link_cb = nullptr;
static lv_obj_t *s_spinner     = nullptr;

static void link_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED && s_link_cb) {
        const char *url = (const char *)lv_event_get_user_data(e);
        s_link_cb(url);
    }
}

void page_clear(lv_obj_t *container) {
    lv_obj_clean(container);
    s_spinner = nullptr;
}

void page_show_spinner(lv_obj_t *container) {
    page_clear(container);
    s_spinner = lv_spinner_create(container, 1000, 60);
    lv_obj_set_size(s_spinner, 80, 80);
    lv_obj_center(s_spinner);
}

void page_render(lv_obj_t *container, const ParseResult *result,
                 link_tap_cb_t on_link_tap) {
    page_clear(container);
    s_link_cb = on_link_tap;

    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container, 8, 0);
    lv_obj_set_style_pad_gap(container, 6, 0);

    for (int i = 0; i < result->count; i++) {
        const PageElement *e = &result->elems[i];

        switch (e->type) {
            case ELEM_HEADING: {
                lv_obj_t *lbl = lv_label_create(container);
                lv_label_set_text(lbl, e->text);
                lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
                lv_obj_set_width(lbl, LV_PCT(100));
                lv_obj_set_style_text_color(lbl, COLOUR_HEADING, 0);
                const lv_font_t *font = &lv_font_montserrat_14;
                if (e->level == 1)      font = &lv_font_montserrat_24;
                else if (e->level == 2) font = &lv_font_montserrat_20;
                else if (e->level == 3) font = &lv_font_montserrat_18;
                else                    font = &lv_font_montserrat_16;
                lv_obj_set_style_text_font(lbl, font, 0);
                lv_obj_set_style_pad_top(lbl, 8, 0);
                break;
            }

            case ELEM_PARAGRAPH: {
                lv_obj_t *lbl = lv_label_create(container);
                lv_label_set_text(lbl, e->text);
                lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
                lv_obj_set_width(lbl, LV_PCT(100));
                lv_obj_set_style_text_color(lbl, COLOUR_TEXT, 0);
                lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
                break;
            }

            case ELEM_LINK: {
                lv_obj_t *btn = lv_btn_create(container);
                lv_obj_set_width(btn, LV_PCT(100));
                lv_obj_set_height(btn, LV_SIZE_CONTENT);
                lv_obj_set_style_bg_color(btn, COLOUR_LINK_BG, 0);
                lv_obj_set_style_border_width(btn, 0, 0);
                lv_obj_set_style_radius(btn, 4, 0);
                lv_obj_set_style_pad_all(btn, 6, 0);

                lv_obj_t *lbl = lv_label_create(btn);
                lv_label_set_text(lbl, e->text);
                lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
                lv_obj_set_width(lbl, LV_PCT(100));
                lv_obj_set_style_text_color(lbl, COLOUR_LINK, 0);
                lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);

                // Pass href as user data — it lives in PSRAM ParseResult pool
                lv_obj_add_event_cb(btn, link_event_cb, LV_EVENT_CLICKED,
                                    (void *)e->href);
                break;
            }

            case ELEM_LINEBREAK: {
                // Small spacer
                lv_obj_t *spacer = lv_obj_create(container);
                lv_obj_set_size(spacer, LV_PCT(100), 4);
                lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(spacer, 0, 0);
                break;
            }
        }
    }
}
```

**Step 3: Verify by wiring into main.cpp**

```cpp
// After fetch + parse, call:
lv_obj_t *scr = lv_scr_act();
lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A2E), 0);

lv_obj_t *container = lv_obj_create(scr);
lv_obj_set_size(container, 480, 440);
lv_obj_set_pos(container, 0, 40);
lv_obj_set_scroll_dir(container, LV_DIR_VER);
lv_obj_set_style_bg_color(container, lv_color_hex(0x1A1A2E), 0);

page_render(container, result, [](const char *url) {
    Serial.printf("Link tapped: %s\n", url);
});
```

**Step 4: Compile, flash, verify page renders correctly on screen. Links should be tappable and print to serial.**

**Step 5: Commit**

```bash
git add src/page_renderer.h src/page_renderer.cpp src/main.cpp
git commit -m "feat: LVGL page renderer (headings, paragraphs, tappable links)"
```

---

### Task 11: Header Bar (back, forward, URL bar)

**Files:**
- Create: `src/ui_header.h`
- Create: `src/ui_header.cpp`

**Step 1: Create `src/ui_header.h`**

```c
// 060326 Header bar: back/forward buttons + URL textarea
#pragma once
#include <lvgl.h>

typedef void (*navigate_cb_t)(const char *url);
typedef void (*back_cb_t)();
typedef void (*forward_cb_t)();

// Creates header bar as child of parent at y=0, h=40
lv_obj_t *header_create(lv_obj_t *parent,
                         navigate_cb_t on_navigate,
                         back_cb_t on_back,
                         forward_cb_t on_forward);

void header_set_url(const char *url);
void header_set_back_enabled(bool en);
void header_set_forward_enabled(bool en);
void header_set_loading(bool loading); // disables buttons during load
```

**Step 2: Create `src/ui_header.cpp`**

```cpp
// 060326 Header bar implementation
#include "ui_header.h"
#include <Arduino.h>

static lv_obj_t *s_back_btn    = nullptr;
static lv_obj_t *s_fwd_btn     = nullptr;
static lv_obj_t *s_url_ta      = nullptr;
static lv_obj_t *s_keyboard    = nullptr;

static navigate_cb_t s_nav_cb  = nullptr;
static back_cb_t     s_back_cb = nullptr;
static forward_cb_t  s_fwd_cb  = nullptr;

static void back_btn_cb(lv_event_t *e) {
    if (s_back_cb) s_back_cb();
}

static void fwd_btn_cb(lv_event_t *e) {
    if (s_fwd_cb) s_fwd_cb();
}

static void url_ta_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_FOCUSED) {
        // Show keyboard
        lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(s_keyboard, s_url_ta);
        // Select all text
        lv_textarea_set_cursor_pos(s_url_ta, LV_TEXTAREA_CURSOR_LAST);
    }
    if (code == LV_EVENT_READY) {
        // Enter pressed
        const char *url = lv_textarea_get_text(s_url_ta);
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(s_url_ta, LV_STATE_FOCUSED);
        if (s_nav_cb && url && *url) s_nav_cb(url);
    }
}

static void keyboard_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(s_url_ta, LV_STATE_FOCUSED);
        if (code == LV_EVENT_READY && s_nav_cb) {
            s_nav_cb(lv_textarea_get_text(s_url_ta));
        }
    }
}

lv_obj_t *header_create(lv_obj_t *parent,
                          navigate_cb_t on_navigate,
                          back_cb_t on_back,
                          forward_cb_t on_forward) {
    s_nav_cb  = on_navigate;
    s_back_cb = on_back;
    s_fwd_cb  = on_forward;

    // Header container
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, 480, 40);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x0F3460), 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 4, 0);
    lv_obj_set_scrollbar_mode(hdr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    // Back button
    s_back_btn = lv_btn_create(hdr);
    lv_obj_set_size(s_back_btn, 32, 32);
    lv_obj_set_pos(s_back_btn, 0, 0);
    lv_obj_set_style_bg_color(s_back_btn, lv_color_hex(0x16213E), 0);
    lv_obj_add_event_cb(s_back_btn, back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(s_back_btn);
    lv_label_set_text(bl, LV_SYMBOL_LEFT);
    lv_obj_center(bl);

    // Forward button
    s_fwd_btn = lv_btn_create(hdr);
    lv_obj_set_size(s_fwd_btn, 32, 32);
    lv_obj_set_pos(s_fwd_btn, 36, 0);
    lv_obj_set_style_bg_color(s_fwd_btn, lv_color_hex(0x16213E), 0);
    lv_obj_add_event_cb(s_fwd_btn, fwd_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *fl = lv_label_create(s_fwd_btn);
    lv_label_set_text(fl, LV_SYMBOL_RIGHT);
    lv_obj_center(fl);

    // URL text area
    s_url_ta = lv_textarea_create(hdr);
    lv_obj_set_size(s_url_ta, 400, 32);
    lv_obj_set_pos(s_url_ta, 72, 0);
    lv_textarea_set_one_line(s_url_ta, true);
    lv_textarea_set_placeholder_text(s_url_ta, "Enter URL...");
    lv_obj_set_style_bg_color(s_url_ta, lv_color_hex(0x16213E), 0);
    lv_obj_set_style_text_color(s_url_ta, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_border_color(s_url_ta, lv_color_hex(0x4FC3F7), 0);
    lv_obj_add_event_cb(s_url_ta, url_ta_cb, LV_EVENT_ALL, NULL);

    // Keyboard (hidden by default, parented to root screen)
    s_keyboard = lv_keyboard_create(lv_scr_act());
    lv_obj_set_size(s_keyboard, 480, 200);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_keyboard, keyboard_cb, LV_EVENT_ALL, NULL);

    return hdr;
}

void header_set_url(const char *url) {
    if (s_url_ta && url) lv_textarea_set_text(s_url_ta, url);
}

void header_set_back_enabled(bool en) {
    if (!s_back_btn) return;
    if (en) lv_obj_clear_state(s_back_btn, LV_STATE_DISABLED);
    else    lv_obj_add_state(s_back_btn, LV_STATE_DISABLED);
}

void header_set_forward_enabled(bool en) {
    if (!s_fwd_btn) return;
    if (en) lv_obj_clear_state(s_fwd_btn, LV_STATE_DISABLED);
    else    lv_obj_add_state(s_fwd_btn, LV_STATE_DISABLED);
}

void header_set_loading(bool loading) {
    header_set_back_enabled(!loading);
    header_set_forward_enabled(!loading);
    if (s_url_ta) {
        if (loading) lv_obj_add_state(s_url_ta, LV_STATE_DISABLED);
        else         lv_obj_clear_state(s_url_ta, LV_STATE_DISABLED);
    }
}
```

**Step 3: Compile, flash, verify header renders, keyboard appears on URL bar tap, Enter hides it**

**Step 4: Commit**

```bash
git add src/ui_header.h src/ui_header.cpp
git commit -m "feat: header bar with back/forward buttons and auto-hiding keyboard"
```

---

### Task 12: Gesture Detection (swipe back/forward)

**Files:**
- Create: `src/gesture.h`
- Create: `src/gesture.cpp`

**Step 1: Create `src/gesture.h`**

```c
// 060326 Swipe gesture detection on LVGL content area
#pragma once
#include <lvgl.h>

typedef void (*swipe_left_cb_t)();
typedef void (*swipe_right_cb_t)();

// Attach gesture detection to obj (the scrollable content container)
void gesture_attach(lv_obj_t *obj,
                    swipe_left_cb_t  on_swipe_left,
                    swipe_right_cb_t on_swipe_right);
```

**Step 2: Create `src/gesture.cpp`**

```cpp
// 060326 Swipe gesture: >50px horizontal, <30px vertical drift
#include "gesture.h"
#include <Arduino.h>

#define SWIPE_MIN_DIST  50
#define SWIPE_MAX_DRIFT 30

static swipe_left_cb_t  s_left_cb  = nullptr;
static swipe_right_cb_t s_right_cb = nullptr;
static lv_coord_t s_press_x = 0;
static lv_coord_t s_press_y = 0;
static bool s_pressing = false;

static void gesture_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    if (code == LV_EVENT_PRESSED) {
        s_press_x  = point.x;
        s_press_y  = point.y;
        s_pressing = true;
    }
    else if (code == LV_EVENT_RELEASED && s_pressing) {
        s_pressing = false;
        lv_coord_t dx = point.x - s_press_x;
        lv_coord_t dy = point.y - s_press_y;
        lv_coord_t adx = abs(dx);
        lv_coord_t ady = abs(dy);

        if (adx >= SWIPE_MIN_DIST && ady <= SWIPE_MAX_DRIFT) {
            if (dx < 0 && s_left_cb)  s_left_cb();   // swipe left = back
            if (dx > 0 && s_right_cb) s_right_cb();  // swipe right = forward
        }
    }
    else if (code == LV_EVENT_PRESS_LOST) {
        s_pressing = false;
    }
}

void gesture_attach(lv_obj_t *obj,
                    swipe_left_cb_t on_swipe_left,
                    swipe_right_cb_t on_swipe_right) {
    s_left_cb  = on_swipe_left;
    s_right_cb = on_swipe_right;
    lv_obj_add_event_cb(obj, gesture_event_cb, LV_EVENT_ALL, NULL);
}
```

**Step 3: Compile, flash, verify swipe left/right triggers serial output**

**Step 4: Commit**

```bash
git add src/gesture.h src/gesture.cpp
git commit -m "feat: swipe gesture detection for back/forward navigation"
```

---

### Task 13: Network FreeRTOS Task

**Files:**
- Create: `src/net_task.h`
- Create: `src/net_task.cpp`

**Step 1: Create `src/net_task.h`**

```c
// 060326 Network task: async fetch + parse on core 0
#pragma once
#include "html_parser.h"

typedef void (*page_ready_cb_t)(ParseResult *result, const char *url);

// Start the network task. page_ready_cb called from net task when page loaded.
// WARNING: page_ready_cb runs on core 0 — use lvgl_lock() before touching LVGL.
void net_task_start(page_ready_cb_t on_page_ready);

// Request a page load (non-blocking, posts to queue)
void net_task_load(const char *url);
```

**Step 2: Create `src/net_task.cpp`**

```cpp
// 060326 FreeRTOS network task on core 0
#include "net_task.h"
#include "fetcher.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#define URL_MAX_LEN 512

static QueueHandle_t    s_url_queue  = nullptr;
static page_ready_cb_t  s_ready_cb   = nullptr;

static void net_task_fn(void *arg) {
    char url_buf[URL_MAX_LEN];

    for (;;) {
        if (xQueueReceive(s_url_queue, url_buf, portMAX_DELAY) == pdTRUE) {
            Serial.printf("NetTask: loading %s\n", url_buf);

            char *html;
            int n = fetch_page(url_buf, &html);

            ParseResult *result = parse_result_alloc();
            if (n > 0 && result) {
                html_parse(html, url_buf, result);
            }

            if (s_ready_cb) s_ready_cb(result, url_buf);
        }
    }
}

void net_task_start(page_ready_cb_t on_page_ready) {
    s_ready_cb  = on_page_ready;
    s_url_queue = xQueueCreate(3, URL_MAX_LEN);
    xTaskCreatePinnedToCore(net_task_fn, "net_task", 16384, nullptr, 4, nullptr, 0);
    Serial.println("Network task started on core 0");
}

void net_task_load(const char *url) {
    if (!s_url_queue) return;
    char buf[URL_MAX_LEN];
    strncpy(buf, url, URL_MAX_LEN - 1);
    buf[URL_MAX_LEN - 1] = '\0';
    xQueueSend(s_url_queue, buf, pdMS_TO_TICKS(500));
}
```

**Step 3: Compile, flash, verify network task starts and fetches**

**Step 4: Commit**

```bash
git add src/net_task.h src/net_task.cpp
git commit -m "feat: async network FreeRTOS task on core 0 with URL queue"
```

---

### Task 14: Full Integration

**Files:**
- Rewrite: `src/main.cpp`
- Rewrite: `src/ui_task.cpp` (add full UI wiring)

**Step 1: Rewrite `src/ui_task.cpp` with full browser UI**

```cpp
// 060326 Full browser UI wiring
#include "ui_task.h"
#include "ui_header.h"
#include "page_renderer.h"
#include "gesture.h"
#include "history.h"
#include "net_task.h"
#include "display.h"
#include "touch.h"
#include <Arduino.h>
#include <lvgl.h>
#include <freertos/semphr.h>

#define HOMEPAGE "https://www.textise.net/"

static SemaphoreHandle_t s_lvgl_mutex = nullptr;
static lv_obj_t *s_content  = nullptr;
static ParseResult *s_current_result = nullptr;

bool lvgl_lock(uint32_t ms) {
    return xSemaphoreTakeRecursive(s_lvgl_mutex, pdMS_TO_TICKS(ms)) == pdTRUE;
}
void lvgl_unlock() {
    xSemaphoreGiveRecursive(s_lvgl_mutex);
}

// ---- Navigation actions ----
static void do_navigate(const char *url);

static void on_back() {
    const char *url = history_back();
    if (url) do_navigate(url);
}

static void on_forward() {
    const char *url = history_forward();
    if (url) do_navigate(url);
}

static void on_link_tap(const char *url) {
    do_navigate(url);
}

static void update_nav_buttons() {
    header_set_back_enabled(history_can_back());
    header_set_forward_enabled(history_can_forward());
}

static void do_navigate(const char *url) {
    history_push(url);
    header_set_url(url);
    update_nav_buttons();
    if (lvgl_lock(50)) {
        page_show_spinner(s_content);
        header_set_loading(true);
        lvgl_unlock();
    }
    net_task_load(url);
}

// ---- Called from net task when page is ready ----
// Runs on core 0 — must lock LVGL
static void on_page_ready(ParseResult *result, const char *url) {
    if (s_current_result) {
        parse_result_free(s_current_result);
    }
    s_current_result = result;

    if (lvgl_lock(200)) {
        header_set_loading(false);
        if (result && result->count > 0) {
            page_render(s_content, result, on_link_tap);
        } else {
            page_clear(s_content);
            lv_obj_t *lbl = lv_label_create(s_content);
            lv_label_set_text(lbl, "Failed to load page.");
            lv_obj_center(lbl);
        }
        update_nav_buttons();
        lvgl_unlock();
    }
}

// ---- WiFi portal screen ----
static void show_portal_screen() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl,
        "No WiFi configured.\n\n"
        "Connect to WiFi AP:\n"
        "\"ESP32-Browser\"\n\n"
        "Then open 192.168.4.1\nto configure WiFi.");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xE0E0E0), 0);
    lv_obj_center(lbl);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, 400);
}

// ---- Build full browser UI ----
void ui_build_root() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A2E), 0);

    // Header
    header_create(scr, do_navigate, on_back, on_forward);

    // Scrollable content area
    s_content = lv_obj_create(scr);
    lv_obj_set_size(s_content, 480, 440);
    lv_obj_set_pos(s_content, 0, 40);
    lv_obj_set_style_bg_color(s_content, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_border_width(s_content, 0, 0);
    lv_obj_set_style_radius(s_content, 0, 0);
    lv_obj_set_style_pad_all(s_content, 0, 0);
    lv_obj_set_scroll_dir(s_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_content, LV_SCROLLBAR_MODE_AUTO);

    // Gesture detection on content area
    gesture_attach(s_content, on_back, on_forward);

    update_nav_buttons();
}

static void ui_task_fn(void *arg) {
    lv_init();
    display_init();
    display_lvgl_init();
    touch_init();
    touch_lvgl_init();

    history_init();

    ui_build_root();

    // Start network task
    net_task_start(on_page_ready);

    // Navigate to homepage
    do_navigate(HOMEPAGE);

    for (;;) {
        if (lvgl_lock(10)) {
            lv_timer_handler();
            lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void ui_task_start() {
    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    xTaskCreatePinnedToCore(ui_task_fn, "ui_task", 12288, nullptr, 5, nullptr, 1);
}
```

**Step 2: Rewrite `src/main.cpp`**

```cpp
// 060326 Browser entry point: WiFi first, then UI task
#include <Arduino.h>
#include "wifi_mgr.h"
#include "ui_task.h"

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("ESP32 Text Browser v1.0");

    wifi_mgr_init();

    if (!wifi_mgr_connect()) {
        Serial.println("No known AP — starting portal");
        // Portal is blocking; after it completes WiFi is connected
        wifi_mgr_start_portal();
    }

    Serial.printf("WiFi: %s\n",
                  wifi_mgr_is_connected() ? "connected" : "failed");

    ui_task_start();
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
```

**Step 3: Compile**

```bash
pio run
```

Fix any errors. Common issues:
- Missing `extern` declarations for `lvgl_lock`/`lvgl_unlock` — add to `ui_task.h`
- Stack overflow: increase task stack in `xTaskCreatePinnedToCore` call
- PSRAM allocation failure: check `MALLOC_CAP_SPIRAM` is available

**Step 4: Flash and verify full flow**

```bash
pio run --target upload
pio device monitor
```

Expected sequence:
1. Serial: "ESP32 Text Browser v1.0"
2. WiFi connects (or portal launches)
3. Screen shows header + spinner
4. textise.net homepage loads and renders
5. Links tappable → navigate to new page
6. Back/forward swipes work
7. URL bar tappable → keyboard appears → can type URL → Enter navigates

**Step 5: Commit**

```bash
git add src/main.cpp src/ui_task.h src/ui_task.cpp
git commit -m "feat: full browser integration — WiFi, fetch, parse, render, navigate"
```

---

### Task 15: Polish & Edge Cases

**Files:**
- Modify: `src/net_task.cpp`
- Modify: `src/html_parser.cpp`
- Modify: `src/ui_task.cpp`

**Step 1: Add fetch error message to net_task**

In `net_task_fn`, if `n <= 0`, populate a minimal ParseResult with an error paragraph:

```cpp
if (n <= 0) {
    // Build error page
    if (result) {
        result->count = 0;
        const char *msg = "Error: could not load page. Check your connection.";
        result->elems[0].type  = ELEM_PARAGRAPH;
        result->elems[0].text  = result->text_pool;
        result->elems[0].href  = NULL;
        result->elems[0].level = 0;
        strncpy(result->text_pool, msg, sizeof(result->text_pool) - 1);
        result->count = 1;
        result->pool_used = strlen(msg) + 1;
    }
}
```

**Step 2: Scroll to top on new page load**

In `on_page_ready`, after `page_render()`:
```cpp
lv_obj_scroll_to(s_content, 0, 0, LV_ANIM_OFF);
```

**Step 3: Handle WiFiManager portal during UI running**

If WiFi drops and portal is needed after startup, show the portal screen before blocking:
```cpp
// In main.cpp before ui_task_start():
// If after portal WiFi is still not connected, show error but continue
// (user can still see the portal screen rendered by LVGL)
```

**Step 4: Compile, flash, verify error page shows on bad URL**

Type a bad URL in the URL bar, verify error message appears instead of crash.

**Step 5: Commit**

```bash
git add src/net_task.cpp src/html_parser.cpp src/ui_task.cpp
git commit -m "fix: error page on fetch failure, scroll-to-top on navigation"
```

---

## Quick Reference

### Compile & flash
```bash
pio run --target upload && pio device monitor
```

### Check PSRAM available
```cpp
Serial.printf("PSRAM free: %zu\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
```

### lv_conf.h custom allocator fix
LVGL's `LV_MEM_CUSTOM_ALLOC` with `heap_caps_malloc_prefer` needs the MALLOC_CAP args. Replace with a wrapper:
```cpp
// In lv_conf.h:
#define LV_MEM_CUSTOM_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
```

### Display not turning on
- Check backlight pin 38 is set HIGH after panel init
- ST7701S SPI init must run before RGB panel `esp_lcd_panel_init()`
- Pixel clock too high can cause instability — try 12MHz if 16MHz fails

### Touch not responding
- `bb_captouch` auto-detects GT911 at 0x5D — verify with I2C scanner sketch
- If `getSamples()` always returns 0, check SDA=19, SCL=45

### textise.net returns empty
- Some URLs redirect; textise.net may return a 301/302 — HTTPClient follows 3 redirects by default; increase with `http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS)`
