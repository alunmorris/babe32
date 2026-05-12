# USB HID Keyboard Host — Implementation Plan
## 170426

### Background
BLE keyboard rejected: NimBLE consumes ~62KB internal SRAM, forces mbedTLS into PSRAM,
and WiFi/BLE radio conflicts degrade page-load speed even with full disconnect during
fetches. USB HID host has none of these problems — it is electrically independent of WiFi.

Source reference: SLUG/src/hal_s2.cpp (ESP32-S2 Mini, working USB HID host).

---

### Hardware

The ESP32-S3 USB OTG peripheral (GPIO 19 D-, GPIO 20 D+) must be wired to a USB-A
socket. The Guition board's USB-C port is connected to the same pins and is used for
programming via USB Serial/JTAG (a separate internal controller). Whether both can
coexist on the same physical connector depends on the board's switching circuitry.

Safest approach: wire a USB-A socket directly to GPIO 19/20 pads with 5V VBUS
from the board's 5V rail. Keyboard must be self-powered or powered from VBUS.
Check board schematic for 5V availability.

---

### Software changes

#### 1. New platformio environment `jc3248w535c_usb_kb`

Add to `platformio.ini`:
```ini
[env:jc3248w535c_usb_kb]
platform = ${common.platform}
board    = ${common.board}
...
build_flags =
    ${common.build_flags}
    -DUSB_KEYBOARD
    -DARDUINO_USB_CDC_ON_BOOT=0   ; frees OTG peripheral from CDC device mode
    -DARDUINO_USB_MODE=0          ; OTG in host mode
```
Serial debug falls back to the USB Serial/JTAG controller (still ttyACM0).

#### 2. New files: `src/usb_kb.h` + `src/usb_kb.cpp`

Port SLUG hal_s2.cpp USB section directly. Key components:

**usb_kb.h** — public API:
```cpp
#ifdef USB_KEYBOARD
void usb_kb_init();          // start daemon + client tasks; waits up to 5s for device
bool usb_kb_connected();
bool usb_kb_poll(BleKbEvent* ev);  // reuse BleKbEvent type (same event set)
#else
inline void usb_kb_init() {}
inline bool usb_kb_connected() { return false; }
inline bool usb_kb_poll(BleKbEvent*) { return false; }
#endif
```

Reuse `BleKbEvent` / `BleKbEventType` from `ble_kb.h` — the event set is identical
(CHAR, BACKSPACE, ENTER, ESCAPE, CURSOR_LEFT/RIGHT, SCROLL_UP/DOWN, URL_FOCUS).

**usb_kb.cpp** — ported from hal_s2.cpp:
- `hidToAscii()` — identical to S2 (already identical to ble_kb.cpp)
- `parseHidReport()` — same key mappings as ble_kb.cpp hidNotifyCB; add key-repeat
  suppression (S2 pattern: compare keycodes with `s_lastKeycodes`)
- Ring buffer — same as ble_kb.cpp
- `findHidEndpoint()` — copy verbatim from S2
- `openDevice()` — copy from S2; replace `dbgLine()` calls with `Serial.println()`
- `closeDevice()` — copy verbatim
- `usbHostDaemonTask()` — copy verbatim
- `usbClientTask()` — copy verbatim; add `sendLedReport()` for Caps Lock LED
- `usb_kb_init()`: create daemon task (priority 2) + client task (priority 1),
  wait up to 5s for `s_devOpen`
- No `before_fetch` / `after_fetch` needed — USB is electrically independent of WiFi

#### 3. `src/ui_task.cpp`

Replace `ble_kb_poll` calls with `usb_kb_poll` (or a thin wrapper that calls whichever
is compiled in). The BLE keyboard event-handling block added in the bluetooth branch
can be reused verbatim since the event type is the same.

Replace `ble_kb_init()` / `ble_kb_start_reconnect()` calls with `usb_kb_init()`.
No reconnect task needed — USB host handles hotplug automatically via `s_newDev` /
`s_devGone` flags in `usbClientTask`.

Remove `ble_kb_before_fetch()` / `ble_kb_after_fetch()` from `net_task.cpp`.

#### 4. `src/net_task.cpp`

Remove `#include "ble_kb.h"` and the before/after fetch calls.

---

### Key differences from SLUG S2

| | SLUG S2 | Browser |
|---|---|---|
| Display debug | `dbgLine()` on TFT | `Serial.println()` |
| Event type | `InputEvent` / `INPUT_*` | `BleKbEvent` / `BLE_KB_*` |
| Init location | `halInit()` | `ui_task_fn()` before WiFi |
| Before/after fetch | `esp_wifi_set_ps()` toggle | Not needed |
| Reconnect | Not applicable | USB hotplug handles it |

---

### Files to create/modify

| File | Action |
|------|--------|
| `src/usb_kb.h` | Create |
| `src/usb_kb.cpp` | Create (port from hal_s2.cpp) |
| `src/ui_task.cpp` | Replace ble_kb_init/poll with usb_kb equivalents |
| `src/net_task.cpp` | Remove before/after fetch calls |
| `platformio.ini` | Add `jc3248w535c_usb_kb` env |

`src/ble_kb.cpp` and `src/ble_kb.h` unchanged — kept behind `#ifdef BLE_KEYBOARD`.
