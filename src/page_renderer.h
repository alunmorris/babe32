// 060326 Build LVGL widgets from parsed PageElement list
#pragma once
#include <lvgl.h>
#include "html_parser.h"

typedef void (*link_tap_cb_t)(const char *url);
typedef void (*form_submit_cb_t)(const char *action_url, bool is_post,
                                  const char *encoded_body);
typedef void (*form_field_focus_cb_t)(lv_obj_t *textarea);

// Render page into container. Clears existing children first.
void page_render(lv_obj_t *container, const ParseResult *result,
                 link_tap_cb_t on_link_tap,
                 form_submit_cb_t on_form_submit,
                 form_field_focus_cb_t on_field_focus);

// Collect form data from container into URL-encoded string
void collect_form_data(lv_obj_t *container, const ParseResult *result,
                       char *out, size_t out_len);

// Clear the page container
void page_clear(lv_obj_t *container);

// Show loading spinner in container
void page_show_spinner(lv_obj_t *container);
