# Code Tidying and Image Scroll Performance

## Goals

1. Reduce `ui_task.cpp` (956 lines) by extracting cohesive modules
2. Eliminate duplicated code (Stop button, url_encode, button styling)
3. Fix scroll jank caused by synchronous image fetching on the UI thread

## 1. File Splitting

### kb_maps.h (new)
- Move keyboard map arrays and control arrays (kb_map_lc, kb_ctrl_lc, kb_map_uc, kb_ctrl_uc, kb_map_spec, kb_ctrl_spec) plus KB_BTN/KB_SPEC macros
- Header-only with static const arrays, included by ui_task.cpp

### boot_menu.cpp/.h (new)
- Move: show_boot_menu(), show_wiki_search(), wiki_search_cb(), menu item data (s_menu[]), menu_item_cb()
- Interface: init function taking callbacks (do_navigate, on_field_focus), plus show_boot_menu() and show_wiki_search()
- Depends on page_renderer.h for colour macros and inversion state

### ui_buttons.cpp/.h (new)
- Single helper: `lv_obj_t *create_flat_btn(lv_obj_t *parent, const char *text, int w, int h, lv_event_cb_t cb, void *user_data)`
- Applies the standard flat button style (bg_opa, bg_color, text_color, text_align, pad_top, radius=0, shadow=0, border=0, clickable)
- Used by Stop button, Retry button, Search button

## 2. Deduplication

### Stop button
- Extract `create_stop_button(lv_obj_t *container)` using create_flat_btn
- Called from load_url() and on_form_submit() — currently identical 13-line blocks

### url_encode()
- Move to url_utils.cpp/.h (already exists with url_resolve())
- Remove copies from page_renderer.cpp and image_fetch.cpp
- Both copies have the same signature and behaviour

### Misc
- Remove duplicate `#include "dbglog.h"` in ui_task.cpp

## 3. Background Image Fetch Task

### Problem
image_fetch() is called synchronously in the UI loop (ui_task_fn). Each fetch takes 1-2s for TLS + HTTP, during which LVGL cannot process touch or render frames, causing scroll jank.

### Solution
New FreeRTOS task `img_task` on core 0 that owns image fetching.

### img_task.cpp/.h (new)
- FreeRTOS task pinned to core 0, stack 8192 bytes
- Owns the WiFiClientSecure connection to webmashing.com
- Request queue: `{int index, const char *url, bool full_size}` — capacity 16
- Result queue: `{int index, uint8_t *data, size_t len, bool full_size}` — capacity 4
- Task loop: block on request queue, call image_fetch()/image_fetch_full(), post result

### image_fetch.cpp changes
- No structural change — still contains fetch_resized(), ensure_connected(), url_encode (removed, use url_utils)
- Called by img_task instead of ui_task

### ui_task.cpp changes
- After page_render(), post all pending image URLs to request queue
- Each loop iteration: check result queue with xQueueReceive(0 timeout), call page_img_set() or page_img_full_set()
- Remove direct image_fetch() calls and the blocking fetch loop
- Full-size image requests also posted to the same queue (full_size=true)

### Data flow
```
UI task (core 1)                img_task (core 0)
  |                                  |
  page_render() populates        blocks on request queue
  image slots                        |
  |                                  |
  posts URLs to request queue -->  receives request
  |                                fetches via TLS (1-2s)
  continues LVGL rendering          |
  checks result queue (0ms)    <-- posts result
  calls page_img_set()               |
```

### Connection management
- img_task calls fetch_disconnect() before connecting (same as current image_fetch)
- When fetcher needs to connect, it calls image_fetch_disconnect() (same as current)
- Mutual exclusion via the existing disconnect pattern — no mutex needed since only one side fetches at a time
