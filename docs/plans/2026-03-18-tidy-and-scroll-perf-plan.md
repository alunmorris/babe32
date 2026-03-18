# Code Tidying and Image Scroll Performance — Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce ui_task.cpp size by extracting modules, eliminate duplicated code, and move image fetching to a background task so scrolling stays smooth while images load.

**Architecture:** Extract keyboard maps, boot menu, and button styling into separate files. Consolidate duplicated url_encode() into url_utils. Add a FreeRTOS task on core 0 for image fetching, communicating with the UI task via queues.

**Tech Stack:** ESP32-S3, FreeRTOS, LVGL 8.4.0, PlatformIO, Arduino framework

**Build command:** `/home/alun/.platformio/penv/bin/platformio run -e jc3248w535c`

**Upload command:** `/home/alun/.platformio/penv/bin/platformio run -e jc3248w535c -t upload`

---

## Chunk 1: Deduplication and Helpers

### Task 1: Move url_encode() to url_utils

**Files:**
- Modify: `src/url_utils.h`
- Modify: `src/url_utils.cpp`
- Modify: `src/page_renderer.cpp` (remove url_encode, add #include "url_utils.h")
- Modify: `src/image_fetch.cpp` (remove url_encode, add #include "url_utils.h")

- [ ] **Step 1: Add url_encode() to url_utils.h**

Add after the `url_get_base()` declaration:

```cpp
// URL-encode src into out buffer. Returns bytes written (excluding null).
size_t url_encode(const char *src, char *out, size_t out_len);
```

- [ ] **Step 2: Add url_encode() implementation to url_utils.cpp**

Add at end of file. Use the page_renderer.cpp version (it handles `+` for spaces, which both callers need):

```cpp
size_t url_encode(const char *src, char *out, size_t out_len) {
    static const char hex[] = "0123456789ABCDEF";
    size_t w = 0;
    for (; *src && w < out_len - 3; src++) {
        char c = *src;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[w++] = c;
        } else if (c == ' ') {
            out[w++] = '+';
        } else {
            out[w++] = '%';
            out[w++] = hex[(uint8_t)c >> 4];
            out[w++] = hex[(uint8_t)c & 0x0F];
        }
    }
    out[w] = '\0';
    return w;
}
```

- [ ] **Step 3: Remove url_encode() from page_renderer.cpp**

Delete the `url_encode()` function (lines 117-135) and add `#include "url_utils.h"` to the includes.

- [ ] **Step 4: Remove url_encode() from image_fetch.cpp**

Delete the `url_encode()` function (lines 12-24) and add `#include "url_utils.h"` to the includes.

- [ ] **Step 5: Build to verify**

Run: `/home/alun/.platformio/penv/bin/platformio run -e jc3248w535c`
Expected: BUILD SUCCESS

- [ ] **Step 6: Commit**

```bash
git add src/url_utils.h src/url_utils.cpp src/page_renderer.cpp src/image_fetch.cpp
git commit -m "Move url_encode() to url_utils, remove duplicates"
```

---

### Task 2: Create ui_buttons helper

**Files:**
- Create: `src/ui_buttons.h`
- Create: `src/ui_buttons.cpp`

- [ ] **Step 1: Create src/ui_buttons.h**

```cpp
// 180326 Flat button helper for consistent UI styling
#pragma once
#include <lvgl.h>

// Create a flat-styled clickable label button.
// Applies: bg_opa cover, bg_color 0x0F3460, text_color 0x4FC3F7,
// text_align center, pad_top 8, radius 0, shadow 0, border 0, clickable.
lv_obj_t *create_flat_btn(lv_obj_t *parent, const char *text,
                           int w, int h,
                           lv_event_cb_t cb, void *user_data);
```

- [ ] **Step 2: Create src/ui_buttons.cpp**

```cpp
// 180326 Flat button helper
#include "ui_buttons.h"

lv_obj_t *create_flat_btn(lv_obj_t *parent, const char *text,
                           int w, int h,
                           lv_event_cb_t cb, void *user_data) {
    lv_obj_t *btn = lv_label_create(parent);
    lv_label_set_text(btn, text);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x0F3460), 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(0x4FC3F7), 0);
    lv_obj_set_style_text_align(btn, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(btn, 8, 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    return btn;
}
```

- [ ] **Step 3: Build to verify**

Run: `/home/alun/.platformio/penv/bin/platformio run -e jc3248w535c`
Expected: BUILD SUCCESS

- [ ] **Step 4: Commit**

```bash
git add src/ui_buttons.h src/ui_buttons.cpp
git commit -m "Add create_flat_btn() helper for consistent button styling"
```

---

### Task 3: Replace duplicated Stop button and other flat buttons with helper

**Files:**
- Modify: `src/ui_task.cpp`

- [ ] **Step 1: Add include**

Add `#include "ui_buttons.h"` to the includes in ui_task.cpp.

- [ ] **Step 2: Replace Stop button in load_url()**

Replace lines 361-373 (the Stop button creation block inside `load_url()`) with:

```cpp
        create_flat_btn(s_content, "Stop", 80, 32, stop_btn_cb, NULL);
```

- [ ] **Step 3: Replace Stop button in on_form_submit()**

Replace lines 411-423 (the identical Stop button block inside `on_form_submit()`) with:

```cpp
            create_flat_btn(s_content, "Stop", 80, 32, stop_btn_cb, NULL);
```

- [ ] **Step 4: Replace Retry button in the error page section**

Replace the Retry button creation block (13 lines near line 839) with:

```cpp
                    lv_obj_t *btn = create_flat_btn(s_content, "Retry", 80, 32, retry_btn_cb, NULL);
```

Note: the inverted colour variant (bg 0xE0E0E0, text 0x0066CC) means the Retry button currently respects inversion. Keep the flat_btn call and then override colours after:

```cpp
                    lv_obj_t *btn = create_flat_btn(s_content, "Retry", 80, 32, retry_btn_cb, NULL);
                    if (inv) {
                        lv_obj_set_style_bg_color(btn, lv_color_hex(0xE0E0E0), 0);
                        lv_obj_set_style_text_color(btn, lv_color_hex(0x0066CC), 0);
                    }
```

- [ ] **Step 5: Remove duplicate #include "dbglog.h"**

Remove the second `#include "dbglog.h"` at line 18 (keep the one at line 15).

- [ ] **Step 6: Build to verify**

Run: `/home/alun/.platformio/penv/bin/platformio run -e jc3248w535c`
Expected: BUILD SUCCESS

- [ ] **Step 7: Commit**

```bash
git add src/ui_task.cpp
git commit -m "Use create_flat_btn() for Stop/Retry buttons, remove duplicate include"
```

---

## Chunk 2: File Splitting

### Task 4: Extract keyboard maps to kb_maps.h

**Files:**
- Create: `src/kb_maps.h`
- Modify: `src/ui_task.cpp`

- [ ] **Step 1: Create src/kb_maps.h**

Move lines 31-78 from ui_task.cpp into a new header. Include the macros and all six arrays:

```cpp
// 180326 Custom 5-row keyboard maps (extracted from ui_task.cpp)
#pragma once
#include <lvgl.h>

#define KB_BTN(w) (LV_BTNMATRIX_CTRL_POPOVER | (w))
#define KB_SPEC   (LV_BTNMATRIX_CTRL_NO_REPEAT | LV_BTNMATRIX_CTRL_CLICK_TRIG | LV_BTNMATRIX_CTRL_CHECKED)

static const char * const kb_map_lc[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_UP, "\n",
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
    "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_NEW_LINE, "\n",
    "1#", "z", "x", "c", "v", "b", "n", "m", ".", "/", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};
static const lv_btnmatrix_ctrl_t kb_ctrl_lc[] = {
    KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_SPEC | 4,
    KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), LV_BTNMATRIX_CTRL_CHECKED | 6,
    KB_SPEC | 6, KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), LV_BTNMATRIX_CTRL_CHECKED | 6,
    KB_SPEC | 5, KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4),
    KB_SPEC | 2, LV_BTNMATRIX_CTRL_CHECKED | 2, 6, LV_BTNMATRIX_CTRL_CHECKED | 2, KB_SPEC | 2
};

static const char * const kb_map_uc[] = {
    "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", LV_SYMBOL_UP, "\n",
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", LV_SYMBOL_NEW_LINE, "\n",
    "1#", "Z", "X", "C", "V", "B", "N", "M", ",", ":", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};
static const lv_btnmatrix_ctrl_t kb_ctrl_uc[] = {
    KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_SPEC | 4,
    KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), LV_BTNMATRIX_CTRL_CHECKED | 6,
    KB_SPEC | 6, KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), LV_BTNMATRIX_CTRL_CHECKED | 6,
    KB_SPEC | 5, KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4),
    KB_SPEC | 2, LV_BTNMATRIX_CTRL_CHECKED | 2, 6, LV_BTNMATRIX_CTRL_CHECKED | 2, KB_SPEC | 2
};

static const char * const kb_map_spec[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_UP, "\n",
    "+", "-", "*", "/", "=", "%", "!", "?", "#", "@", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "&", "_", "\\", "|", "(", ")", "[", "]", ";", LV_SYMBOL_NEW_LINE, "\n",
    "<", ">", "{", "}", "\"", "'", "~", "`", "$", "^", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};
static const lv_btnmatrix_ctrl_t kb_ctrl_spec[] = {
    KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_SPEC | 4,
    KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), LV_BTNMATRIX_CTRL_CHECKED | 6,
    KB_SPEC | 6, KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), LV_BTNMATRIX_CTRL_CHECKED | 6,
    KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4), KB_BTN(4),
    KB_SPEC | 2, LV_BTNMATRIX_CTRL_CHECKED | 2, 6, LV_BTNMATRIX_CTRL_CHECKED | 2, KB_SPEC | 2
};
```

- [ ] **Step 2: Replace arrays in ui_task.cpp**

Delete lines 31-78 (the KB_BTN/KB_SPEC macros and all six arrays) and add `#include "kb_maps.h"` after the existing includes.

- [ ] **Step 3: Build to verify**

Run: `/home/alun/.platformio/penv/bin/platformio run -e jc3248w535c`
Expected: BUILD SUCCESS

- [ ] **Step 4: Commit**

```bash
git add src/kb_maps.h src/ui_task.cpp
git commit -m "Extract keyboard maps to kb_maps.h"
```

---

### Task 5: Extract boot menu to boot_menu.cpp/.h

**Files:**
- Create: `src/boot_menu.h`
- Create: `src/boot_menu.cpp`
- Modify: `src/ui_task.cpp`

- [ ] **Step 1: Create src/boot_menu.h**

```cpp
// 180326 Boot menu and wiki search screens (extracted from ui_task.cpp)
#pragma once
#include <lvgl.h>

typedef void (*navigate_cb_t)(const char *url);
typedef void (*field_focus_cb_t)(lv_obj_t *ta);

// Initialise with content container and callbacks. Must be called before show_*.
void boot_menu_init(lv_obj_t *content, navigate_cb_t nav, field_focus_cb_t focus);

// Display the boot menu (home screen with pig image and menu items).
void show_boot_menu();

// Display the Wikipedia search page.
void show_wiki_search();

// Get the wiki search textarea (needed by keyboard Enter handler in ui_task).
// Returns nullptr if wiki search is not currently shown.
lv_obj_t *boot_menu_get_wiki_ta();
```

- [ ] **Step 2: Create src/boot_menu.cpp**

Move the following from ui_task.cpp into boot_menu.cpp:
- `s_menu[]` array and `s_menu_count`
- `menu_item_cb()`
- `wiki_search_cb()`
- `show_wiki_search()`
- `extern const lv_img_dsc_t babe32_img;`
- `show_boot_menu()`
- `enable_urls_mode()` — this sets `s_show_links` in ui_task.cpp, so instead expose it: boot_menu.cpp calls the navigate callback which already handles this in ui_task via `do_navigate()`. For Search/Wikipedia items that need URLs mode enabled, add a simple bool flag callback or handle it in ui_task's do_navigate wrapper.

The file needs:
- File-scope statics: `s_content`, `s_nav_cb`, `s_focus_cb`, `s_urls_mode_cb`, `s_wiki_ta`
- Includes: lvgl.h, page_renderer.h, ui_header.h, ui_buttons.h, Arduino.h
- Access to `lvgl_lock`/`lvgl_unlock` — declare as `extern bool lvgl_lock(uint32_t ms); extern void lvgl_unlock();`
- Access to `enable_urls_mode()` — keep this function in ui_task.cpp, pass it as part of init via callback.
- `s_aichat_home` stays in ui_task.cpp — it's created lazily (only on AI chat page load). The `if (s_aichat_home) lv_obj_add_flag(s_aichat_home, LV_OBJ_FLAG_HIDDEN)` lines are moved to ui_task.cpp's `on_home()` wrapper, which already calls `show_boot_menu()`. Similarly for show_wiki_search — ui_task hides s_aichat_home before calling it.

Updated header:
```cpp
// 180326 Boot menu and wiki search screens (extracted from ui_task.cpp)
#pragma once
#include <lvgl.h>

typedef void (*navigate_cb_t)(const char *url);
typedef void (*field_focus_cb_t)(lv_obj_t *ta);
typedef void (*urls_mode_cb_t)();

void boot_menu_init(lv_obj_t *content, navigate_cb_t nav,
                    field_focus_cb_t focus, urls_mode_cb_t urls_mode);
void show_boot_menu();
void show_wiki_search();
lv_obj_t *boot_menu_get_wiki_ta();
```

The .cpp file moves the functions listed above, replacing direct references to ui_task statics with the stored callbacks. The `s_wiki_ta` from ui_task.cpp is replaced by a local static in boot_menu.cpp, accessed via `boot_menu_get_wiki_ta()`.

- [ ] **Step 3: Update ui_task.cpp**

- Remove the moved functions and data (show_boot_menu, show_wiki_search, wiki_search_cb, menu_item_cb, s_menu, enable_urls_mode body)
- Add `#include "boot_menu.h"`
- In `ui_build_root()`, after creating s_content, call:
  ```cpp
  boot_menu_init(s_content, do_navigate, on_field_focus, enable_urls_mode);
  ```
- Replace `s_wiki_ta` references with `boot_menu_get_wiki_ta()`:
  - In `on_back()`: `if (boot_menu_get_wiki_ta()) { on_home(); return; }`
  - In `kb_event_cb()`: `else if (boot_menu_get_wiki_ta() && ta == boot_menu_get_wiki_ta())`
  - In `load_url()`: remove `s_wiki_ta = nullptr;` (boot_menu.cpp manages its own state — show_boot_menu/show_wiki_search set it, load_url doesn't need to clear it since page_clear destroys the widget)
- Update `on_home()` to hide s_aichat_home before calling `show_boot_menu()`:
  ```cpp
  static void on_home() {
      if (s_aichat_home) lv_obj_add_flag(s_aichat_home, LV_OBJ_FLAG_HIDDEN);
      show_boot_menu();
  }
  ```
- Similarly, where show_wiki_search() is triggered (from menu_item_cb, now in boot_menu.cpp), the aichat_home hide is handled because boot_menu's show_wiki_search() no longer references it — and the wiki search is only reachable from boot_menu where aichat_home is already hidden
- Keep `enable_urls_mode()` as a static function in ui_task.cpp (passed via callback)

- [ ] **Step 4: Build to verify**

Run: `/home/alun/.platformio/penv/bin/platformio run -e jc3248w535c`
Expected: BUILD SUCCESS

- [ ] **Step 5: Commit**

```bash
git add src/boot_menu.h src/boot_menu.cpp src/ui_task.cpp
git commit -m "Extract boot menu and wiki search to boot_menu.cpp"
```

---

## Chunk 3: Background Image Fetch Task

### Task 6: Create img_task.cpp/.h

**Files:**
- Create: `src/img_task.h`
- Create: `src/img_task.cpp`

- [ ] **Step 1: Create src/img_task.h**

```cpp
// 180326 Background image fetch task — fetches images on core 0 via queues
#pragma once
#include <stddef.h>
#include <stdint.h>

struct ImgRequest {
    int index;              // slot index, or -1 for full-size
    char url[512];          // copied URL (safe if page navigates during fetch)
    bool full_size;
};

struct ImgResult {
    int index;
    uint8_t *data;          // PSRAM allocation, receiver frees. nullptr on failure.
    size_t len;
    bool full_size;
};

// Start the background image fetch task (call once at startup).
void img_task_start();

// Post a fetch request (non-blocking, drops if queue full).
void img_task_post(const ImgRequest *req);

// Poll for a completed result (non-blocking).
// Returns true and fills *out if a result is available.
bool img_task_poll(ImgResult *out);

// Flush pending requests (call on page navigation to cancel queued fetches).
void img_task_flush();
```

- [ ] **Step 2: Create src/img_task.cpp**

```cpp
// 180326 Background image fetch task
#include "img_task.h"
#include "image_fetch.h"
#include "dbglog.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

static QueueHandle_t s_req_queue  = nullptr;
static QueueHandle_t s_res_queue  = nullptr;

static void img_task_fn(void *arg) {
    for (;;) {
        ImgRequest req;
        if (xQueueReceive(s_req_queue, &req, portMAX_DELAY) == pdTRUE) {
            size_t len = 0;
            uint8_t *data;
            if (req.full_size)
                data = image_fetch_full(req.url, &len);
            else
                data = image_fetch(req.url, &len);

            ImgResult res;
            res.index = req.index;
            res.data = data;
            res.len = len;
            res.full_size = req.full_size;
            // Post result — if queue full, free data to avoid leak
            if (xQueueSend(s_res_queue, &res, 0) != pdTRUE) {
                if (data) heap_caps_free(data);
                dbg("img_task: result queue full, dropped");
            }
        }
    }
}

void img_task_start() {
    s_req_queue = xQueueCreate(16, sizeof(ImgRequest));
    s_res_queue = xQueueCreate(4, sizeof(ImgResult));
    xTaskCreatePinnedToCore(img_task_fn, "img_task", 12288, nullptr, 3, nullptr, 0);
}

void img_task_post(const ImgRequest *req) {
    if (s_req_queue)
        xQueueSend(s_req_queue, req, 0);  // non-blocking
}

bool img_task_poll(ImgResult *out) {
    if (!s_res_queue) return false;
    return xQueueReceive(s_res_queue, out, 0) == pdTRUE;
}

void img_task_flush() {
    if (!s_req_queue) return;
    ImgRequest discard;
    while (xQueueReceive(s_req_queue, &discard, 0) == pdTRUE) {}
    // Also drain results and free any allocated data
    ImgResult res;
    while (xQueueReceive(s_res_queue, &res, 0) == pdTRUE) {
        if (res.data) heap_caps_free(res.data);
    }
}
```

- [ ] **Step 3: Build to verify**

Run: `/home/alun/.platformio/penv/bin/platformio run -e jc3248w535c`
Expected: BUILD SUCCESS (img_task.cpp compiles but is not yet called)

- [ ] **Step 4: Commit**

```bash
git add src/img_task.h src/img_task.cpp
git commit -m "Add img_task: background FreeRTOS task for image fetching"
```

---

### Task 7: Wire img_task into ui_task.cpp

**Files:**
- Modify: `src/ui_task.cpp`

- [ ] **Step 1: Add include and start task**

Add `#include "img_task.h"` to ui_task.cpp includes.

In `ui_task_fn()`, after `net_task_start(on_page_ready);` (line 777), add:

```cpp
    img_task_start();
```

- [ ] **Step 2: Post image requests after page_render()**

In the main loop, after `page_render()` is called (line 810-811), add code to post pending images:

```cpp
                    // Queue pending images for background fetch
                    if (s_show_images) {
                        int img_idx;
                        const char *img_url;
                        while (page_img_next(&img_idx, &img_url)) {
                            ImgRequest req = {};
                            req.index = img_idx;
                            strncpy(req.url, img_url, sizeof(req.url) - 1);
                            req.full_size = false;
                            img_task_post(&req);
                        }
                    }
```

- [ ] **Step 3: Poll for image results in the loop**

Replace the existing blocking thumbnail fetch section (lines 933-947):

```cpp
        // Fetch one pending thumbnail per loop (outside LVGL lock)
        if (!s_loading && s_show_images) {
            int img_idx;
            const char *img_url;
            if (page_img_next(&img_idx, &img_url)) {
                size_t len = 0;
                uint8_t *data = image_fetch(img_url, &len);
                if (data && len > 0 && lvgl_lock(50)) {
                    page_img_set(img_idx, data, len);
                    lvgl_unlock();
                } else if (data) {
                    heap_caps_free(data);
                }
            }
        }
```

With non-blocking poll:

```cpp
        // Check for completed image fetches (non-blocking)
        {
            ImgResult res;
            while (img_task_poll(&res)) {
                if (lvgl_lock(50)) {
                    if (res.full_size) {
                        if (res.data && res.len > 0)
                            page_img_full_set(res.data, res.len);
                        else {
                            if (res.data) heap_caps_free(res.data);
                            page_img_full_fail();
                        }
                    } else {
                        if (res.data && res.len > 0)
                            page_img_set(res.index, res.data, res.len);
                        else if (res.data)
                            heap_caps_free(res.data);
                    }
                    lvgl_unlock();
                } else if (res.data) {
                    heap_caps_free(res.data);
                }
            }
        }
```

- [ ] **Step 4: Replace full-size image fetch with queue post**

Replace the existing blocking full-size fetch section (lines 916-931):

```cpp
        // Full-size image fetch (outside LVGL lock, highest priority)
        {
            const char *full_url;
            if (page_img_full_pending(&full_url)) {
                size_t len = 0;
                uint8_t *data = image_fetch_full(full_url, &len);
                if (lvgl_lock(50)) {
                    if (data && len > 0)
                        page_img_full_set(data, len);
                    else
                        page_img_full_fail();
                    lvgl_unlock();
                } else if (data) {
                    heap_caps_free(data);
                }
            }
        }
```

With a queue post:

```cpp
        // Full-size image request — post to background task
        {
            const char *full_url;
            if (page_img_full_pending(&full_url)) {
                ImgRequest req = {};
                req.index = -1;
                strncpy(req.url, full_url, sizeof(req.url) - 1);
                req.full_size = true;
                img_task_post(&req);
            }
        }
```

Note: The URL is copied into ImgRequest (512-byte buffer), so it remains valid even if the page navigates and page_clear() frees the text_pool while img_task is still fetching.

- [ ] **Step 5: Flush image queue on page navigation**

In `load_url()`, before `net_task_load(url)`, add:

```cpp
    img_task_flush();
```

This cancels any queued image fetches for the old page.

- [ ] **Step 6: Remove image_fetch.h include if no longer needed directly**

Check if ui_task.cpp still needs `#include "image_fetch.h"`. After these changes, ui_task.cpp no longer calls `image_fetch()` or `image_fetch_full()` directly, but it still uses `IMAGE_THUMB_W` etc. from image_fetch.h (no — those are used in page_renderer.cpp only). Remove the include if unused.

Actually, check: `image_fetch_disconnect()` is not called from ui_task.cpp (it's called from fetcher.cpp's `ensure_connected()`). And `image_fetch.h` constants are used in page_renderer.cpp. So ui_task.cpp may not need it directly — verify by removing and building.

- [ ] **Step 7: Build to verify**

Run: `/home/alun/.platformio/penv/bin/platformio run -e jc3248w535c`
Expected: BUILD SUCCESS

- [ ] **Step 8: Upload and test**

Run: `/home/alun/.platformio/penv/bin/platformio run -e jc3248w535c -t upload`

Test by:
1. Navigate to a page with images (e.g. Hackaday)
2. Enable IMGs toggle
3. Scroll while images are loading — scrolling should remain smooth
4. Tap an image to view full-size — overlay should appear with loading text, then image

- [ ] **Step 9: Commit**

```bash
git add src/ui_task.cpp
git commit -m "Wire img_task into UI loop: non-blocking image fetch via queues"
```

---

### Task 8: Remove url_encode from image_fetch.cpp (if not done in Task 1)

This was already handled in Task 1. Verify `image_fetch.cpp` uses `#include "url_utils.h"` and has no local `url_encode()`.

- [ ] **Step 1: Verify**

Read `src/image_fetch.cpp` and confirm url_encode is gone and url_utils.h is included.

- [ ] **Step 2: Final build and upload**

Run: `/home/alun/.platformio/penv/bin/platformio run -e jc3248w535c`
Expected: BUILD SUCCESS

Run: `/home/alun/.platformio/penv/bin/platformio run -e jc3248w535c -t upload`

- [ ] **Step 3: Commit if any changes**

Only if Step 1 found issues.
