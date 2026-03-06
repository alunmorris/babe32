# ESP32 Text Browser — Design Document

060326 Initial design

## Overview

A minimal web browser for the Guition/Sunton ESP32-4848S0 (ESP32-S3, 480x480 RGB display). Uses textise.net as a proxy to fetch text-only versions of web pages. Navigation via touch gestures, auto-hiding on-screen keyboard, WiFi with multi-AP support.

## Hardware

| Component | Detail |
|-----------|--------|
| MCU | ESP32-S3, dual-core 240MHz |
| Flash | 16MB QIO |
| PSRAM | 8MB OPI high-speed |
| Display | 480x480 ST7701S, RGB parallel interface |
| Touch | GT911, I2C addr 0x5D |
| Board package | rzeldent/platformio-espressif32-sunton |

### Pin Definitions

```c
// RGB LCD control
#define LCD_DE    18
#define LCD_VSYNC 17
#define LCD_HSYNC 16
#define LCD_PCLK  21
#define LCD_BL    38

// RGB data
// R: 4, 5, 6, 7, 15
// G: 8, 20, 3, 46, 9, 10
// B: 11, 12, 13, 14, 0

// ST7701S SPI init
#define LCD_SPI_CS   39
#define LCD_SPI_SCK  48
#define LCD_SPI_MOSI 47

// GT911 touch (INT/RST not connected)
#define TOUCH_SDA 19
#define TOUCH_SCL 45
```

## Architecture

Two FreeRTOS tasks communicate via a message queue:

```
Core 0 — Network Task          Core 1 — UI Task
  WiFi management                LVGL lv_timer_handler()
  HTTPClient fetch               Touch polling (GT911)
  HTML tokenizer                 Widget rendering
  PageElement builder            Gesture detection
        |                               |
        +--------[ FreeRTOS Queue ]-----+
                  PageResult msg
```

### PSRAM Memory Budget

| Region | Size |
|--------|------|
| LVGL frame buffers (x2) | ~900KB |
| Raw HTML page buffer | 512KB |
| Parsed PageElement pool | 128KB |
| History stack (50 URLs) | ~6KB |
| WiFi AP list (10 entries) | ~2KB |

## UI Layout

```
+------------------------------------------+
|  [<]  [>]   [URL bar.....................]  |  <- Header 40px
+------------------------------------------+
|                                          |
|  # Heading (bold, 24px)                  |
|                                          |
|  Paragraph text, word-wrapped, plain     |
|  label...                                |
|                                          |
|  [ Tappable link ]                       |
|                                          |
|  ## Sub-heading (bold, 20px)             |
|                                          |  <- Scrollable area 440px
|  More content...                         |
|                                          |
+------------------------------------------+
|  [ LVGL keyboard — slides up on demand ] |
+------------------------------------------+
```

### Gestures

| Gesture | Action |
|---------|--------|
| Swipe up/down | Scroll content (LVGL native) |
| Swipe left (>50px, <30px drift) | Navigate back |
| Swipe right (>50px, <30px drift) | Navigate forward |
| Tap URL bar | Show keyboard, select all |
| Enter on keyboard | Navigate, hide keyboard |
| Tap outside keyboard | Hide keyboard |

### Loading State

- Content area shows centred `lv_spinner` during fetch
- Back/forward buttons disabled during load

## HTML Parsing

Single-pass tokenizer over the 512KB PSRAM buffer. No DOM tree, no external parser library.

### Tags Handled

| Tag | Rendering |
|-----|-----------|
| `<h1>`–`<h3>` | Bold label, 24/20/18px |
| `<h4>`–`<h6>` | Bold label, 16px |
| `<p>` | Label with bottom margin |
| `<a href="...">` | Accent-coloured tappable label |
| `<br>` | Newline in current label |
| `<strong>`, `<b>` | Bold label segment |
| All others | Stripped, inner text preserved |

### PageElement Struct

```c
typedef enum {
    ELEM_HEADING,
    ELEM_PARAGRAPH,
    ELEM_LINK,
    ELEM_LINEBREAK
} ElemType;

typedef struct {
    ElemType type;
    char    *text;   // pointer into PSRAM text pool
    char    *href;   // resolved absolute URL (ELEM_LINK only)
    uint8_t  level;  // 1-6 (ELEM_HEADING only)
} PageElement;
```

### Link Resolution

- Track `current_base_url` (scheme + host + path directory)
- `href` starting with `http` → use as-is
- `href` starting with `/` → prepend scheme + host
- Relative `href` → prepend `current_base_url`
- All resolved URLs wrapped through textise.net before fetching:
  `https://www.textise.net/showText.aspx?strURL=<resolved_url>`

## Navigation History

- Fixed array of 50 URL strings in PSRAM
- Integer `head` and `current` pointers
- New navigation: advance `head`, clear forward stack
- Back: decrement `current`
- Forward: increment `current` up to `head`

## WiFi Management

### Multi-AP Storage

- Up to 10 AP slots in NVS via `Preferences` library
- Each slot: SSID (64 bytes) + password (64 bytes)
- New credentials appended; oldest evicted when list full

### Startup Flow

1. Load all saved AP credentials from NVS
2. `WiFi.scanNetworks()` — match results against saved list
3. If match found: `WiFi.begin(ssid, pass)`, 10s timeout
4. If connected: launch browser, load homepage
5. If no match or timeout: launch captive portal AP (`ESP32-Browser`, open)
6. Portal UI shown on display: "Connect to ESP32-Browser WiFi to configure"
7. User submits credentials via captive portal → save to NVS → connect → launch browser

### Homepage

`https://www.textise.net/`

## Build Configuration

```ini
[env:esp32-4848s0]
platform = espressif32
platform_packages =
    platformio/framework-arduinoespressif32
    ; rzeldent sunton board definitions
board = esp32-s3-4848S040C   ; from rzeldent/platformio-espressif32-sunton
framework = arduino
board_build.psram_type = opi
board_build.flash_mode = qio
board_build.flash_size = 16MB
board_upload.flash_size = 16MB
board_build.partitions = partitions_16MB.csv

lib_deps =
    lvgl/lvgl @ ^8.3.11
    tzapu/WiFiManager @ ^2.0.17
    bitbank2/bb_captouch       ; GT911 auto-detect touch library

build_flags =
    -DBOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue
    -DLVGL_CONF_PATH=\"lv_conf.h\"
    -DLV_MEM_CUSTOM=1
    -DLV_MEM_SIZE=0            ; unlimited (PSRAM allocator)
```

## Key Libraries

| Library | Purpose |
|---------|---------|
| `esp_lcd` (built-in) | RGB panel + SPI init driver for ST7701S |
| `lvgl` 8.3.x | UI widgets, scroll, keyboard, gestures |
| `WiFiManager` | Captive portal for new AP entry |
| `Preferences` | NVS storage for AP credentials |
| `HTTPClient` + `WiFiClientSecure` | HTTPS fetch from textise.net |
| `bb_captouch` | GT911 touch reading |
