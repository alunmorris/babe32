// 060326 LVGL page renderer
#include "page_renderer.h"
#include <Arduino.h>

#define COLOUR_BG      lv_color_hex(0x1A1A2E)
#define COLOUR_TEXT    lv_color_hex(0xE0E0E0)
#define COLOUR_HEADING lv_color_hex(0xFFFFFF)
#define COLOUR_LINK    lv_color_hex(0x4FC3F7)
#define COLOUR_LINK_BG lv_color_hex(0x16213E)

static link_tap_cb_t s_link_cb = nullptr;

static void link_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED && s_link_cb) {
        // Ignore click if LVGL detected a scroll gesture (user was swiping)
        lv_indev_t *indev = lv_indev_get_act();
        if (indev && lv_indev_get_scroll_obj(indev) != NULL) return;
        const char *url = (const char *)lv_event_get_user_data(e);
        if (url) s_link_cb(url);
    }
}

void page_clear(lv_obj_t *container) {
    lv_obj_clean(container);
}

void page_show_spinner(lv_obj_t *container) {
    page_clear(container);
    lv_obj_t *lbl = lv_label_create(container);
    lv_label_set_text(lbl, "Loading...");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xE0E0E0), 0);
    lv_obj_center(lbl);
}

void page_render(lv_obj_t *container, const ParseResult *result,
                 link_tap_cb_t on_link_tap) {
    page_clear(container);
    s_link_cb = on_link_tap;

    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container, 8, 0);
    lv_obj_set_style_pad_gap(container, 4, 0);

    for (int i = 0; i < result->count; i++) {
        const PageElement *e = &result->elems[i];
        if (!e->text && e->type != ELEM_LINEBREAK) continue;

        switch (e->type) {
            case ELEM_HEADING: {
                lv_obj_t *lbl = lv_label_create(container);
                lv_label_set_text(lbl, e->text);
                lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
                lv_obj_set_width(lbl, LV_PCT(100));
                lv_obj_set_style_text_color(lbl, COLOUR_HEADING, 0);
                const lv_font_t *font = &lv_font_montserrat_16;
                if      (e->level == 1) font = &lv_font_montserrat_24;
                else if (e->level == 2) font = &lv_font_montserrat_20;
                else if (e->level == 3) font = &lv_font_montserrat_18;
                lv_obj_set_style_text_font(lbl, font, 0);
                lv_obj_set_style_pad_top(lbl, 6, 0);
                break;
            }
            case ELEM_PARAGRAPH: {
                lv_obj_t *lbl = lv_label_create(container);
                lv_label_set_text(lbl, e->text);
                lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
                lv_obj_set_width(lbl, LV_PCT(100));
                lv_obj_set_style_text_color(lbl, COLOUR_TEXT, 0);
                lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
                break;
            }
            case ELEM_LINK: {
                lv_obj_t *lbl = lv_label_create(container);
                lv_label_set_text(lbl, e->text);
                lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
                lv_obj_set_width(lbl, LV_PCT(100));
                lv_obj_set_style_text_color(lbl, COLOUR_LINK, 0);
                lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
                lv_obj_add_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_event_cb(lbl, link_event_cb, LV_EVENT_CLICKED,
                                    (void *)e->href);
                break;
            }
            case ELEM_LINEBREAK: {
                lv_obj_t *sp = lv_label_create(container);
                lv_obj_set_size(sp, LV_PCT(100), 4);
                lv_label_set_text(sp, "");
                break;
            }
        }
    }
}
