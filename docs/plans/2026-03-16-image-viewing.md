# Image Viewing Feature

## Overview

Add toggleable inline image viewing to the ESP32 text browser. Images are fetched through a server-side resize proxy that returns thumbnails, decoded on-device using LVGL's built-in PNG and JPEG decoders.

## Constants

```c
#define IMAGE_THUMB_W 120
#define IMAGE_THUMB_H 80
#define IMAGE_PROXY_URL "https://webmashing.com/image-resize.php/?url=%s&w=%d&h=%d"
#define IMAGE_CACHE_SLOTS 8
```

## Implementation Plan

### Step 1: Enable LVGL image support

**File**: `src/lv_conf.h`

Add:
```c
#define LV_USE_IMG    1
#define LV_USE_PNG    1
#define LV_USE_SJPG   1
```

Build and verify it compiles without errors.

### Step 2: Add ELEM_IMAGE to parser

**File**: `src/html_parser.h`

- Add `ELEM_IMAGE` to `ElemType` enum

**File**: `src/html_parser.cpp`

- In the tag handling section (after `<a>` handling), add `<img>` parsing:
  - Extract `src` attribute using existing `get_attr()`
  - Extract `alt` attribute for fallback text
  - Resolve `src` to absolute URL using `url_resolve()`
  - Call `add_elem()` with type `ELEM_IMAGE`, store src URL in `href` field, alt text in `text` field
  - `<img>` is a void element (no closing tag), so handle inline like `<br>`

### Step 3: Add IMGs toggle button

**File**: `src/ui_task.cpp`

- Add `static bool s_show_images = false`
- Add `static lv_obj_t *s_img_btn = nullptr`
- Create "IMGs" button in the keyboard setup section, positioned 10px above URLs button
  - URLs is at `BOTTOM_RIGHT, -4, -42`, so IMGs at `BOTTOM_RIGHT, -4, -82` (approx)
- Add `img_toggle_cb()` — toggles `s_show_images`, re-renders current page (same pattern as `url_toggle_cb`)
- Show/hide IMGs button alongside URLs and Keyboard buttons
- Pass `s_show_images` to `page_render()`

### Step 4: Image fetcher

**File**: `src/image_fetch.h` (new)

```c
#pragma once
#include <stddef.h>
#include <stdint.h>

#define IMAGE_THUMB_W 120
#define IMAGE_THUMB_H 80

// Fetch a resized image via proxy. Returns PSRAM-allocated buffer with
// raw image file data (PNG/JPEG), or nullptr on failure.
// *out_len receives the data length. Caller must free with heap_caps_free().
uint8_t *image_fetch(const char *img_url, size_t *out_len);
```

**File**: `src/image_fetch.cpp` (new)

- Build proxy URL: `https://webmashing.com/image-resize.php/?url=<encoded_url>&w=120&h=80`
- URL-encode the image src
- Use a small dedicated PSRAM buffer (~32KB) for the HTTP response
- Connect directly to webmashing.com via TLS (like POST path in fetcher.cpp)
- Parse HTTP response headers, extract body
- Return body data (PNG/JPEG bytes) allocated in PSRAM
- Caller frees after LVGL decodes

### Step 5: Image rendering in page_renderer

**File**: `src/page_renderer.h`

- Update `page_render()` signature to add `bool show_images = false`

**File**: `src/page_renderer.cpp`

- Add `#include "image_fetch.h"`
- In `page_render()` element loop, add `ELEM_IMAGE` case:
  - If `!show_images`: skip (or show `[img: alt text]` as plain text)
  - If `show_images`:
    - Create `lv_img` widget, set size to IMAGE_THUMB_W × IMAGE_THUMB_H max
    - Set grey background as placeholder
    - Store image URL and widget pointer in a pending-fetch list
- After render loop, fetch images sequentially:
  - For each pending image, call `image_fetch(url, &len)`
  - Create `lv_img_dsc_t` descriptor with the raw file data
  - Call `lv_img_set_src(widget, &descriptor)` to display
  - LVGL's PNG/SJPG decoder handles format detection from file header

### Step 6: Image cache

**File**: `src/image_cache.h` (new)
**File**: `src/image_cache.cpp` (new)

- Simple fixed-size cache (8 slots)
- Each slot: URL hash (uint32_t), decoded `lv_img_dsc_t`, PSRAM data pointer
- `image_cache_get(url)` — returns cached descriptor or nullptr
- `image_cache_put(url, data, len)` — stores decoded image, evicts LRU if full
- `image_cache_clear()` — frees all slots (call on page navigation)

## Rendering Flow

1. User enables IMGs → `s_show_images = true` → page re-renders
2. Parser has already extracted `ELEM_IMAGE` elements with src URLs
3. Renderer creates placeholder widgets, queues fetch list
4. For each image (sequentially):
   - Check cache → hit: use cached data
   - Miss: call `image_fetch()` → get raw PNG/JPEG bytes
   - Create `lv_img_dsc_t`, set as image source
   - Cache the result
5. User disables IMGs → re-renders without images

## Memory Budget

- Per image decoded: ~19.2KB max (120×80×2 RGB565)
- Cache (8 slots): ~154KB PSRAM
- Fetch buffer: ~32KB PSRAM (reused per image)
- Total: ~186KB PSRAM (from ~6.4MB available)

## Edge Cases

- Images that fail to load: show alt text or "[img]" placeholder
- Pages with many images: fetch first 8-10, skip rest
- Cancel during image fetch: stop fetching remaining images
- Navigate away during image fetch: cancel and clear queue
