// 060326 LVGL page renderer
// 130326 Add form widget rendering (input, select, submit)
#include "page_renderer.h"
#include <Arduino.h>
#include <string.h>

#define COLOUR_BG      lv_color_hex(0x1A1A2E)
#define COLOUR_TEXT    lv_color_hex(0xE0E0E0)
#define COLOUR_HEADING lv_color_hex(0xFFFFFF)
#define COLOUR_LINK    lv_color_hex(0x4FC3F7)
#define COLOUR_LINK_BG lv_color_hex(0x16213E)
#define COLOUR_FIELD   lv_color_hex(0x16213E)
#define COLOUR_BORDER  lv_color_hex(0x0F3460)
#define COLOUR_BTN_BG  lv_color_hex(0x0F3460)
#define COLOUR_BTN_TXT lv_color_hex(0x4FC3F7)

static link_tap_cb_t        s_link_cb       = nullptr;
static form_submit_cb_t     s_submit_cb     = nullptr;
static form_field_focus_cb_t s_focus_cb     = nullptr;
static const ParseResult   *s_cur_result    = nullptr;
static lv_obj_t            *s_cur_container = nullptr;

static void link_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED && s_link_cb) {
        lv_indev_t *indev = lv_indev_get_act();
        if (indev && lv_indev_get_scroll_obj(indev) != NULL) return;
        const char *url = (const char *)lv_event_get_user_data(e);
        if (url) s_link_cb(url);
    }
}

static void form_ta_click_cb(lv_event_t *e) {
    if (s_focus_cb) s_focus_cb(lv_event_get_target(e));
}

// URL-encode src into out. Returns bytes written (excluding null).
static size_t url_encode(const char *src, char *out, size_t out_len) {
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

// Collect form data from container's textareas/dropdowns + hidden fields
void collect_form_data(lv_obj_t *container, const ParseResult *result,
                               char *out, size_t out_len) {
    size_t w = 0;
    // Hidden fields first
    for (int i = 0; i < result->count && w < out_len - 1; i++) {
        const PageElement *e = &result->elems[i];
        if (e->type != ELEM_HIDDEN || !e->name) continue;
        if (w > 0) out[w++] = '&';
        w += url_encode(e->name, out + w, out_len - w);
        out[w++] = '=';
        if (e->value) w += url_encode(e->value, out + w, out_len - w);
    }
    // Walk children for textareas and dropdowns
    uint32_t child_cnt = lv_obj_get_child_cnt(container);
    for (uint32_t ci = 0; ci < child_cnt && w < out_len - 1; ci++) {
        lv_obj_t *child = lv_obj_get_child(container, ci);
        const char *fname = (const char *)lv_obj_get_user_data(child);
        if (!fname || fname[0] == '\0') continue;

        const char *val = nullptr;
        char dd_buf[128];
        if (lv_obj_check_type(child, &lv_textarea_class)) {
            val = lv_textarea_get_text(child);
        } else if (lv_obj_check_type(child, &lv_dropdown_class)) {
            lv_dropdown_get_selected_str(child, dd_buf, sizeof(dd_buf));
            val = dd_buf;
        }
        if (!val) continue;

        if (w > 0) out[w++] = '&';
        w += url_encode(fname, out + w, out_len - w);
        out[w++] = '=';
        w += url_encode(val, out + w, out_len - w);
    }
    out[w] = '\0';
}

static void form_submit_click_cb(lv_event_t *e) {
    if (!s_submit_cb || !s_cur_result || !s_cur_container) return;
    lv_indev_t *indev = lv_indev_get_act();
    if (indev && lv_indev_get_scroll_obj(indev) != NULL) return;

    static char form_data[4096];
    collect_form_data(s_cur_container, s_cur_result, form_data, sizeof(form_data));
    s_submit_cb(s_cur_result->form_action, s_cur_result->form_is_post, form_data);
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
                 link_tap_cb_t on_link_tap,
                 form_submit_cb_t on_form_submit,
                 form_field_focus_cb_t on_field_focus) {
    page_clear(container);
    s_link_cb       = on_link_tap;
    s_submit_cb     = on_form_submit;
    s_focus_cb      = on_field_focus;
    s_cur_result    = result;
    s_cur_container = container;

    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(container, 8, 0);
    lv_obj_set_style_pad_gap(container, 4, 0);

    for (int i = 0; i < result->count; i++) {
        const PageElement *e = &result->elems[i];
        if (e->type == ELEM_HIDDEN) continue;  // not rendered
        if (!e->text && e->type != ELEM_LINEBREAK &&
            e->type != ELEM_INPUT && e->type != ELEM_SELECT) continue;

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
            case ELEM_INPUT: {
                lv_obj_t *ta = lv_textarea_create(container);
                lv_obj_set_width(ta, LV_PCT(100));
                lv_obj_set_height(ta, e->multiline ? 80 : 32);
                lv_textarea_set_one_line(ta, !e->multiline);
                if (e->value) lv_textarea_set_text(ta, e->value);
                if (e->text) lv_textarea_set_placeholder_text(ta, e->text);
                lv_obj_set_style_bg_color(ta, COLOUR_FIELD, 0);
                lv_obj_set_style_text_color(ta, COLOUR_TEXT, 0);
                lv_obj_set_style_text_font(ta, &lv_font_montserrat_14, 0);
                lv_obj_set_style_border_width(ta, 1, 0);
                lv_obj_set_style_border_color(ta, COLOUR_BORDER, 0);
                lv_obj_set_style_radius(ta, 0, 0);
                lv_obj_set_style_shadow_width(ta, 0, 0);
                lv_obj_set_style_outline_width(ta, 0, 0);
                lv_obj_set_style_pad_all(ta, 4, 0);
                lv_obj_set_style_anim_time(ta, 0, 0);
                lv_obj_set_style_bg_color(ta, lv_color_hex(0x4FC3F7),
                                          LV_PART_CURSOR | LV_STATE_FOCUSED);
                lv_obj_set_style_bg_opa(ta, LV_OPA_COVER,
                                        LV_PART_CURSOR | LV_STATE_FOCUSED);
                lv_obj_set_style_border_width(ta, 0, LV_PART_CURSOR);
                lv_obj_set_style_radius(ta, 0, LV_PART_CURSOR);
                lv_obj_set_style_anim_time(ta, 0,
                                           LV_PART_CURSOR | LV_STATE_FOCUSED);
                lv_obj_set_user_data(ta, (void *)e->name);
                lv_obj_add_event_cb(ta, form_ta_click_cb, LV_EVENT_CLICKED, NULL);
                break;
            }
            case ELEM_SUBMIT: {
                lv_obj_t *btn = lv_label_create(container);
                lv_label_set_text(btn, e->text ? e->text : "Submit");
                lv_obj_set_width(btn, LV_SIZE_CONTENT);
                lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
                lv_obj_set_style_bg_color(btn, COLOUR_BTN_BG, 0);
                lv_obj_set_style_text_color(btn, COLOUR_BTN_TXT, 0);
                lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
                lv_obj_set_style_pad_all(btn, 8, 0);
                lv_obj_set_style_radius(btn, 0, 0);
                lv_obj_set_style_shadow_width(btn, 0, 0);
                lv_obj_set_style_border_width(btn, 0, 0);
                lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_event_cb(btn, form_submit_click_cb,
                                    LV_EVENT_CLICKED, NULL);
                break;
            }
            case ELEM_SELECT: {
                lv_obj_t *dd = lv_dropdown_create(container);
                lv_obj_set_width(dd, LV_PCT(100));
                if (e->value) lv_dropdown_set_options(dd, e->value);
                lv_obj_set_style_bg_color(dd, COLOUR_FIELD, 0);
                lv_obj_set_style_text_color(dd, COLOUR_TEXT, 0);
                lv_obj_set_style_text_font(dd, &lv_font_montserrat_14, 0);
                lv_obj_set_style_border_width(dd, 1, 0);
                lv_obj_set_style_border_color(dd, COLOUR_BORDER, 0);
                lv_obj_set_style_radius(dd, 0, 0);
                lv_obj_set_style_shadow_width(dd, 0, 0);
                lv_obj_set_style_pad_all(dd, 4, 0);
                lv_obj_set_user_data(dd, (void *)e->name);
                break;
            }
            default:
                break;
        }
    }
}
