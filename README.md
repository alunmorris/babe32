# Babe32 — Barely Adequate Browser ESP32

A text web browser that runs on a cheap ESP32-S3 touchscreen. Fetches pages via a text-only proxy, renders HTML with LVGL, and lets you navigate the web with your finger on a 480×480 display.

![Babe32 browser on device](docs/images/babe32-device.jpg)
<!-- Add your photo here. Create docs/images/ and drop in a shot of the device. -->

---

## Features

- **Text browsing** — strips pages to readable text via [textise.net](https://www.textise.net/) + a self-hosted PHP proxy
- **HTML rendering** — headings (h1–h6), paragraphs, links, bold/italic/monospace, font sizes, inline colour
- **Image viewing** — inline images fetched via a resize proxy, tappable for full-screen view
- **HTML forms** — text inputs, dropdowns, submit buttons (GET and POST)
- **Touch navigation** — swipe left/right for back/forward; tap links; pull-to-refresh
- **On-screen keyboard** — LVGL keyboard slides up on text focus
- **External keyboard** — BLE HID or USB HID keyboard (compile-time option)
- **WiFi setup UI** — scan APs, enter password, stores up to 10 networks in NVS
- **WiFi signal indicator** — live strength bar in the header
- **Navigation history** — 50-URL back/forward stack
- **Boot menu** — quick Wikipedia search on startup
- **AI Chat** — built-in chat screen backed by a server-side PHP endpoint
- **Power management** — backlight dim → off → light-sleep after inactivity (battery build)

---

## Hardware

| Component | Detail |
|-----------|--------|
| Board | Guition / Sunton JC3248W535C |
| MCU | ESP32-S3, dual-core 240 MHz |
| Flash | 16 MB QIO |
| PSRAM | 8 MB OPI high-speed |
| Display | 480×480 ST7701S, RGB parallel interface |
| Touch | GT911, I²C |

The board is widely sold under several brand names (Guition, Sunton, Elecrow 4.0"). Look for the **JC3248W535C** or **ESP32-S3-4848S040** part number.

---

## Architecture

Two FreeRTOS tasks communicate via a message queue:

```
Core 0 — Network Task              Core 1 — UI Task
  WiFi management                    LVGL lv_timer_handler()
  HTTPS fetch (1 MB PSRAM buffer)    GT911 touch polling
  Single-pass HTML tokenizer         Widget builder
  PageElement array builder          Gesture detection
          |                                  |
          +--------[ FreeRTOS Queue ]--------+
                     PageResult msg
```

PSRAM memory budget:

| Region | Size |
|--------|------|
| LVGL frame buffers (×2) | ~900 KB |
| Raw HTML buffer | 1 MB |
| Parsed PageElement pool | 128 KB |
| Navigation history (50 URLs) | ~6 KB |
| WiFi AP list (10 entries) | ~2 KB |

---

## Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- Linux/macOS/WSL recommended; Windows native should also work
- USB-C cable to the board's programming port

### Build & Flash

```bash
git clone https://github.com/alunmorris/babe32.git
cd babe32

# Default build (base browser)
pio run -e jc3248w535c --target upload

# With USB keyboard support
pio run -e jc3248w535c_usb_kb --target upload

# Battery / power-saving build
pio run -e jc3248w535c_battery --target upload
```

Monitor serial output at 115200 baud:

```bash
pio device monitor
```

### Build Environments

| Environment | Description |
|-------------|-------------|
| `jc3248w535c` | Standard build |
| `jc3248w535c_usb_kb` | USB HID keyboard host |
| `jc3248w535c_battery` | Battery power management (dim/sleep) |

---

## Configuration

### WiFi

On first boot, open the boot menu and tap **WiFi Setup**. The browser scans for networks, you enter the password, and credentials are saved to NVS. Up to 10 networks are stored; the device tries each on startup.

### Proxy Setup (required)

Babe32 fetches pages through two proxies in sequence:

1. **Primary — self-hosted PHP proxy** (`src/server/babe32proxy.php`)  
   Upload this to any PHP web host. Edit `src/fetcher.cpp` to point `PROXY_HOST` at your server.

2. **Fallback — Brightdata residential proxy**  
   Sign up at [brightdata.com](https://brightdata.com), create a residential proxy, and fill in the credentials in `src/fetcher.cpp`.

Without at least one proxy configured the browser will not load pages.

### AI Chat (optional)

Upload `src/server/aichat.php` to your web server and update the endpoint URL in `src/ui_task.cpp`. The chat screen will then be available from the boot menu.

---

## External Keyboard

**BLE keyboard** — pairing happens automatically on first connect. Any BLE HID keyboard should work (tested with standard Bluetooth keyboards).

**USB keyboard** — build with the `jc3248w535c_usb_kb` environment. Connect a USB keyboard via a USB-A OTG adapter to the board's USB port.

| Key | Action |
|-----|--------|
| `Ctrl+L` | Focus URL bar |
| `Enter` (URL bar) | Navigate to URL |
| `Enter` (link focused) | Follow link |
| `Backspace` | Delete / back |
| Arrow keys | Scroll page |

---

## Project Structure

```
babe32/
├── platformio.ini
├── partitions_16MB.csv
├── lv_conf.h
└── src/
    ├── main.cpp               # Entry point
    ├── display.{h,cpp}        # ST7701S RGB panel driver + LVGL flush
    ├── touch.{h,cpp}          # GT911 touch driver + LVGL indev
    ├── ui_task.{h,cpp}        # LVGL FreeRTOS task (core 1)
    ├── net_task.{h,cpp}       # Network FreeRTOS task (core 0)
    ├── wifi_mgr.{h,cpp}       # Multi-AP NVS storage + connect
    ├── wifi_setup.{h,cpp}     # WiFi setup UI (scan, password, save)
    ├── fetcher.{h,cpp}        # HTTPS fetch into PSRAM buffer
    ├── html_parser.{h,cpp}    # Single-pass HTML tokenizer
    ├── page_renderer.{h,cpp}  # LVGL widget builder
    ├── image_fetch.{h,cpp}    # Image fetch via resize proxy
    ├── img_task.{h,cpp}       # Non-blocking image queue
    ├── url_utils.{h,cpp}      # URL resolution + proxy wrapping
    ├── history.{h,cpp}        # 50-URL navigation history
    ├── boot_menu.{h,cpp}      # Boot screen + Wikipedia search
    ├── gesture.{h,cpp}        # Swipe detection
    ├── ble_kb.{h,cpp}         # BLE HID keyboard host
    ├── usb_kb.{h,cpp}         # USB HID keyboard host
    ├── power_mgr.{h,cpp}      # Battery power management
    ├── ui_header.{h,cpp}      # Header bar (back/fwd/URL/signal)
    ├── ui_buttons.{h,cpp}     # Toolbar buttons
    └── server/
        ├── babe32proxy.php    # Self-hosted fetch proxy
        └── aichat.php         # AI chat backend
```

---

## Limitations

- **Text-only by default** — images are opt-in and load slowly over the proxy chain
- **No JavaScript** — pages are rendered server-side text; interactive JS apps won't work
- **No TLS certificate pinning** — the proxy handles TLS; the device trusts the proxy
- **Single tab** — one page at a time
- **480×480 display** — pages are reformatted for a narrow portrait layout

---

## Licence

MIT — see [LICENSE](LICENSE) for details.
