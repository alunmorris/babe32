# Landscape Display Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rotate the browser UI from portrait (320×480) to landscape (480×320) so the user gets a wider browsing experience.

**Architecture:** LVGL renders to a full-frame 480×320 landscape buffer in PSRAM. The flush callback rotates each pixel from landscape→portrait coordinates and writes portrait strips to an internal DMA buffer for the AXS15231B display. Touch coordinates are transformed in the read callback. No hardware MADCTL rotation (broken in QSPI mode) and no LVGL `sw_rotate` (allocates from PSRAM which DMA can't read).

**Tech Stack:** LVGL 8, ESP-IDF/Arduino, AXS15231B QSPI, ESP32-S3

---

## Hardware Constraints (must not violate)

1. **RAMWRC ignores CASET/RASET** — display must be flushed sequentially top-to-bottom starting from y=0 in portrait coordinates (320×480).
2. **SPI2 DMA cannot read PSRAM** — the buffer passed to `esp_lcd_panel_draw_bitmap()` must be `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`.
3. **MADCTL MV+MX corrupts write window** in QSPI mode — no hardware rotation.
4. **LVGL `sw_rotate`** allocates temp buffer from `LV_MEM_CUSTOM` (PSRAM) → DMA deadlock. Cannot use.
5. **Complex widgets** (shadows, gradients, rounded corners) render as noise — keep flat styling.

## Rotation Geometry

**CW 90° rotation** (top of landscape content = right edge of physical portrait panel):

- Landscape coordinates: `lx ∈ [0,479]`, `ly ∈ [0,319]`
- Portrait coordinates: `px ∈ [0,319]`, `py ∈ [0,479]`
- **Landscape→Portrait pixel mapping:** `px = 319 - ly`, `py = lx`
- **Portrait→Landscape touch mapping:** `lx = ty`, `ly = 319 - tx`

## Memory Budget

| Buffer | Size | Location |
|--------|------|----------|
| LVGL draw buf (full landscape frame) | 480×320×2 = 300 KB | PSRAM |
| DMA output strip (portrait) | 320×60×2 = 37.5 KB | Internal RAM |
| **Total internal** | **37.5 KB** | (was 37.5 KB — same) |
| **Total PSRAM** | **300 KB** | (was 0 — new) |

The 300 KB PSRAM draw buffer replaces the previous 37.5 KB internal DMA draw buffer. LVGL draws into PSRAM (fine — no DMA involved in drawing). Only the DMA output strip needs internal RAM.

## File Structure

| File | Action | Responsibility |
|------|--------|---------------|
| `src/display.cpp` | Modify | Landscape LVGL config, PSRAM draw buf, rotation flush_cb |
| `src/display.h` | Modify | Export landscape resolution constants |
| `src/lv_conf.h` | Modify | Update `LV_HOR_RES_MAX` / `LV_VER_RES_MAX` |
| `src/touch.cpp` | Modify | Transform portrait→landscape touch coordinates |
| `src/ui_task.cpp` | Modify | Update layout constants for 480×320 |
| `src/ui_header.cpp` | Modify | Update header width for 480px |

---

## Task 1: Display Driver — Landscape Rotation

**Files:**
- Modify: `src/display.cpp`
- Modify: `src/display.h`
- Modify: `src/lv_conf.h`

### Changes to `src/lv_conf.h`

- [ ] **Step 1: Update resolution constants**

```c
#define LV_HOR_RES_MAX  480
#define LV_VER_RES_MAX  320
```

### Changes to `src/display.h`

- [ ] **Step 2: Update header comment**

Change "320x480 portrait" to "480x320 landscape" in the comment.

### Changes to `src/display.cpp`

The display hardware stays portrait (320×480). LVGL sees landscape (480×320). The flush callback bridges the gap.

- [ ] **Step 3: Add landscape resolution constants**

Keep existing `LCD_H_RES=320` and `LCD_V_RES=480` (physical portrait). Add landscape constants:

```cpp
// Landscape resolution presented to LVGL
#define LANDSCAPE_W 480
#define LANDSCAPE_H 320
```

- [ ] **Step 4: Allocate PSRAM draw buffer + internal DMA output buffer**

Replace the current `display_lvgl_init` buffer allocation. The LVGL draw buffer is now a full landscape frame in PSRAM (LVGL draws here). A separate internal DMA buffer is used for portrait output to the display.

```cpp
static lv_color_t *s_dma_out = nullptr;  // portrait DMA output strip

void display_lvgl_init() {
    // Full landscape frame in PSRAM — LVGL draws here (no DMA involved)
    size_t fb_size = LANDSCAPE_W * LANDSCAPE_H;
    buf1 = (lv_color_t *)heap_caps_malloc(fb_size * sizeof(lv_color_t),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(buf1);

    // Portrait DMA output strip — internal RAM for SPI DMA
    size_t dma_size = LCD_H_RES * DMA_CHUNK_ROWS * sizeof(lv_color_t);
    s_dma_out = (lv_color_t *)heap_caps_malloc(dma_size,
                                                MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(s_dma_out);

    lv_disp_draw_buf_init(&draw_buf, buf1, nullptr, fb_size);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = LANDSCAPE_W;   // 480
    disp_drv.ver_res      = LANDSCAPE_H;   // 320
    disp_drv.flush_cb     = lvgl_flush_cb;
    disp_drv.draw_buf     = &draw_buf;
    disp_drv.full_refresh = 1;  // always full frame (rotation needs whole picture)
    // No rounder_cb needed — full_refresh handles it
    lv_disp_drv_register(&disp_drv);
}
```

- [ ] **Step 5: Rewrite flush_cb with rotation**

The flush callback receives a full 480×320 landscape frame from LVGL (in PSRAM). It rotates pixels into portrait strips and sends them to the display via DMA.

CW 90° rotation: portrait `(px, py) = (319 - ly, lx)`.

Reading by portrait row for cache-friendliness on the output side:

```cpp
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                           lv_color_t *color_map) {
    // color_map is the full 480×320 landscape frame in PSRAM.
    // Rotate to portrait 320×480 and send in DMA_CHUNK_ROWS strips.

    for (int py_start = 0; py_start < LCD_V_RES; py_start += DMA_CHUNK_ROWS) {
        int py_end = py_start + DMA_CHUNK_ROWS;
        if (py_end > LCD_V_RES) py_end = LCD_V_RES;
        int rows = py_end - py_start;

        // Rotate landscape pixels into portrait DMA buffer.
        // Portrait (px, py) ← Landscape (lx, ly) where lx=py, ly=319-px
        for (int py = py_start; py < py_end; py++) {
            int lx = py;  // portrait row → landscape column
            for (int px = 0; px < LCD_H_RES; px++) {
                int ly = (LCD_H_RES - 1) - px;  // 319 - px
                s_dma_out[(py - py_start) * LCD_H_RES + px] =
                    color_map[ly * LANDSCAPE_W + lx];
            }
        }

        // Send strip to display
        xSemaphoreTake(s_flush_done, 0);  // drain stale
        esp_lcd_panel_draw_bitmap(panel_handle,
                                  0, py_start, LCD_H_RES, py_end,
                                  s_dma_out);
        xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(100));
    }

    lv_disp_flush_ready(drv);
}
```

- [ ] **Step 6: Remove rounder_cb**

Delete the `lvgl_rounder_cb` function entirely. With `full_refresh=1` and a full-frame buffer, it's not needed — LVGL always sends the complete frame.

- [ ] **Step 7: Update fill_solid and test functions**

These write directly to the display in portrait coordinates — they stay unchanged. But update any comments referring to resolution.

- [ ] **Step 8: Build and verify**

Run: `pio run`
Expected: Clean build, no errors.

- [ ] **Step 9: Upload and test display**

Run: `pio run -t upload && pio device monitor`

Expected: The browser UI appears rotated 90° clockwise — content flows left-to-right across the 480px width. Touch will be wrong at this point (fixed in Task 2).

- [ ] **Step 10: Commit**

```bash
git add src/display.cpp src/display.h src/lv_conf.h
git commit -m "feat: landscape display via software rotation in flush callback"
```

---

## Task 2: Touch Coordinate Transformation

**Files:**
- Modify: `src/touch.cpp`

The touch controller reports portrait coordinates (tx ∈ [0,319], ty ∈ [0,479]). LVGL expects landscape coordinates (lx ∈ [0,479], ly ∈ [0,319]).

CW 90° mapping: `lx = ty`, `ly = 319 - tx`.

- [ ] **Step 1: Transform coordinates in touch_read_cb**

```cpp
static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    uint16_t tx, ty;
    if (s_touch_ok && axs_read(&tx, &ty)) {
        // Transform portrait touch → landscape coordinates (CW 90°)
        data->point.x = ty;           // landscape x = portrait y
        data->point.y = 319 - tx;     // landscape y = 319 - portrait x
        data->state   = LV_INDEV_STATE_PR;
        g_touch_x = data->point.x;
        g_touch_y = data->point.y;
        g_touch_pressed = true;
    } else {
        data->state = LV_INDEV_STATE_REL;
        g_touch_pressed = false;
    }
}
```

- [ ] **Step 2: Build, upload, test**

Run: `pio run -t upload && pio device monitor`

Expected: Touch targets match visual positions. Tapping the URL bar opens the keyboard. Back button works. Links are clickable.

- [ ] **Step 3: Commit**

```bash
git add src/touch.cpp
git commit -m "feat: transform touch coordinates for landscape orientation"
```

---

## Task 3: UI Layout for Landscape

**Files:**
- Modify: `src/ui_header.cpp`
- Modify: `src/ui_task.cpp`

The header and content area use `LV_HOR_RES` and `LV_VER_RES` which will now be 480 and 320 respectively. Most sizing is already relative, but some values need adjustment for the wider, shorter screen.

- [ ] **Step 1: Adjust header bar (ui_header.cpp)**

The header was 30px tall × 320px wide. Now it should be 30px tall × 480px wide. `LV_HOR_RES` already handles the width. The URL textarea width uses `LV_HOR_RES - 38` which auto-adapts. **No code change needed** — verify by reading that all sizes use `LV_HOR_RES`/`LV_VER_RES` or relative values.

- [ ] **Step 2: Adjust keyboard height (ui_task.cpp)**

The keyboard was 180px tall on a 480px-tall screen (37.5%). On a 320px-tall landscape screen, 180px would leave only 110px for content. Reduce to ~120px:

```cpp
#define KB_HEIGHT 120
```

- [ ] **Step 3: Verify content area sizing**

The content area uses:
- Height: `LV_VER_RES - 30` = 290px (landscape) — fine
- Width: `LV_HOR_RES` = 480px — auto-adapts
- With keyboard: `LV_VER_RES - 30 - KB_HEIGHT` = 170px — usable

These use `LV_VER_RES` and `LV_HOR_RES` so they auto-adapt. **No code change needed** beyond the KB_HEIGHT reduction.

- [ ] **Step 4: Build, upload, test**

Run: `pio run -t upload && pio device monitor`

Expected:
- Header spans full 480px width
- Content area fills remaining height
- Keyboard is 120px tall, leaves enough room for content
- URL bar accepts input, keyboard shows/hides correctly

- [ ] **Step 5: Commit**

```bash
git add src/ui_header.cpp src/ui_task.cpp
git commit -m "feat: adjust UI layout for 480x320 landscape"
```

---

## Task 4: Performance Tuning

After the basic rotation works, assess and optimize if needed.

- [ ] **Step 1: Measure frame rate**

Add timing to flush_cb:
```cpp
uint32_t t0 = millis();
// ... rotation + DMA ...
Serial.printf("flush: %lu ms\n", millis() - t0);
```

Expected: ~15-30ms per full frame (33-66 fps equivalent). If >50ms, proceed to step 2.

- [ ] **Step 2: Optimize rotation inner loop (if needed)**

The inner loop reads landscape pixels column-by-column (stride = LANDSCAPE_W = 480 pixels = 960 bytes). This is cache-unfriendly on the source side. Optimize by processing in tiles or using 32-bit reads:

```cpp
// Process by landscape row for better cache locality on source
for (int ly = LCD_H_RES - 1; ly >= 0; ly--) {
    int px = (LCD_H_RES - 1) - ly;
    const lv_color_t *src_row = &color_map[ly * LANDSCAPE_W + py_start];
    for (int lx = py_start; lx < py_end; lx++) {
        int py = lx;
        s_dma_out[(py - py_start) * LCD_H_RES + px] = src_row[lx - py_start];
    }
}
```

This reads the source sequentially (cache-friendly) at the cost of scattered writes to the DMA buffer (which is in internal SRAM where cache doesn't matter as much).

- [ ] **Step 3: Remove timing code, commit**

```bash
git add src/display.cpp
git commit -m "perf: optimize rotation loop cache access pattern"
```

---

## Verification Checklist

After all tasks are complete:

- [ ] Browser loads in landscape orientation (480×320)
- [ ] Touch coordinates match visual positions
- [ ] Back button, URL bar, links all respond to taps correctly
- [ ] Keyboard shows/hides, keys register correctly
- [ ] Page content scrolls vertically
- [ ] Swipe gestures work (back/forward)
- [ ] No display corruption or tearing
- [ ] Frame rate is acceptable (no visible lag when typing or scrolling)

## Rollback

If landscape rotation causes unfixable issues:
1. Revert `lv_conf.h` to `LV_HOR_RES_MAX=320`, `LV_VER_RES_MAX=480`
2. Revert `display.cpp` to portrait flush_cb with rounder_cb
3. Revert `touch.cpp` to direct portrait coordinates
4. Revert `ui_task.cpp` KB_HEIGHT to 180
