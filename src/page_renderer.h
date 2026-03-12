// 060326 Build LVGL widgets from parsed PageElement list
#pragma once
#include <lvgl.h>
#include "html_parser.h"

typedef void (*link_tap_cb_t)(const char *url);

// Render page into container. Clears existing children first.
void page_render(lv_obj_t *container, const ParseResult *result,
                 link_tap_cb_t on_link_tap);

// Clear the page container
void page_clear(lv_obj_t *container);

// Show loading spinner in container
void page_show_spinner(lv_obj_t *container);
