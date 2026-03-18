# Code Tidying and Image Scroll Performance

## Goals

1. Reduce `ui_task.cpp` (956 lines) by extracting cohesive modules
2. Eliminate duplicated code (Stop button, url_encode, button styling)
3. Fix scroll jank — image fetching on the UI task (core 1) blocks frame rendering for 1-2s per image

## 1. File Splitting

### kb_maps.h (new)
- Move keyboard map arrays and control arrays (kb_map_lc, kb_ctrl_lc, kb_map_uc, kb_ctrl_uc, kb_map_spec, kb_ctrl_spec) plus KB_BTN/KB_SPEC macros
- Header-only with `static const` arrays (single includer — ui_task.cpp — so no duplicate symbol risk)

### boot_menu.cpp/.h (new)
- Move: show_boot_menu(), show_wiki_search(), wiki_search_cb(), menu item data (s_menu[]), menu_item_cb()
- Interface:
  ```cpp
  typedef void (*navigate_cb_t)(const char *url);
  typedef void (*field_focus_cb_t)(lv_obj_t *ta);
  void boot_menu_init(lv_obj_t *content, navigate_cb_t nav, field_focus_cb_t focus);
  void show_boot_menu();
  void show_wiki_search();
  ```
  Callbacks and content pointer stored as file-scope statics in boot_menu.cpp.
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
image_fetch() is called on the UI task (core 1). Although it runs outside the LVGL lock, the entire task is blocked for 1-2s during TLS+HTTP, so no frames get rendered and touch is unresponsive.

### Solution
New FreeRTOS task `img_task` on core 0 that owns image fetching.

### Queue structures
```cpp
struct ImgRequest {
    int index;              // slot index, or -1 for full-size
    const char *url;        // points into ParseResult text_pool (valid while page displayed)
    bool full_size;
};

struct ImgResult {
    int index;              // matches request index
    uint8_t *data;          // PSRAM allocation, caller frees
    size_t len;
    bool full_size;
};
```

### img_task.cpp/.h (new)
- FreeRTOS task pinned to core 0, stack 12288 bytes (WiFiClientSecure TLS needs ~6KB stack)
- Owns the WiFiClientSecure connection to webmashing.com
- Request queue: ImgRequest, capacity 16
- Result queue: ImgResult, capacity 4
- Task loop: block on request queue (portMAX_DELAY), call image_fetch()/image_fetch_full(), post result to result queue. On fetch failure, post result with data=nullptr so UI can handle it.
- `img_task_start()` — creates task and queues
- `img_task_post(ImgRequest *)` — post to request queue
- `img_task_poll(ImgResult *)` — non-blocking check of result queue (returns true if result available)

### image_fetch.cpp changes
- No structural change — still contains fetch_resized(), ensure_connected()
- Remove url_encode() (use url_utils instead)
- Called by img_task instead of ui_task

### ui_task.cpp changes
- After page_render(), iterate pending image slots and post ImgRequest for each
- Each loop iteration: call img_task_poll(), if result arrived call page_img_set() or page_img_full_set()
- Remove direct image_fetch()/image_fetch_full() calls and the blocking fetch sections
- Full-size image requests posted with full_size=true, index=-1

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
  checks result queue (0ms)    <-- posts result (data or nullptr on error)
  calls page_img_set()               |
```

### Connection management
- img_task calls fetch_disconnect() before connecting (same as current image_fetch ensure_connected())
- When fetcher needs to connect, it calls image_fetch_disconnect() (same as current)
- These calls are safe because only one side fetches at a time: page fetch happens during s_loading=true, image fetch happens when s_loading=false and s_show_images=true. They don't overlap.
