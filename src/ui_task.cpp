// 060326 Full browser UI wiring
// 110326 Replace diagnostic loop with full LVGL + browser init
// 120326 Fix cross-core rendering via task notification; sync flush + full invalidate
// 120326 Add on-screen keyboard for URL entry
// 130326 Reduce keyboard height for landscape layout
#include "ui_task.h"
#include "ui_header.h"
#include "page_renderer.h"
#include "gesture.h"
#include "history.h"
#include "net_task.h"
#include "display.h"
#include "touch.h"
#include "dbglog.h"
#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#define HOMEPAGE "https://html.duckduckgo.com/lite"
#define KB_HEIGHT 150

// --- Custom 5-row keyboard maps (number row on top) ---
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

static SemaphoreHandle_t s_lvgl_mutex    = nullptr;
static lv_obj_t         *s_content       = nullptr;
static lv_obj_t         *s_kb            = nullptr;
static lv_obj_t         *s_show_btn      = nullptr;
static bool              s_kb_visible    = false;
static ParseResult      *s_cur_result    = nullptr;
static ParseResult      *s_pending_result = nullptr;
static TaskHandle_t      s_ui_task_handle = nullptr;
static bool              s_loading       = false;
volatile int             g_fetch_kb = 0;

bool lvgl_lock(uint32_t ms) {
    return xSemaphoreTakeRecursive(s_lvgl_mutex, pdMS_TO_TICKS(ms)) == pdTRUE;
}
void lvgl_unlock() {
    xSemaphoreGiveRecursive(s_lvgl_mutex);
}

static void do_navigate(const char *url);
static void load_url(const char *url);
static void retry_current();

static void kb_show();
static void kb_hide();

static void on_back() {
    const char *url = history_back();
    if (url) load_url(url);
}
static void on_forward() {
    const char *url = history_forward();
    if (url) load_url(url);
}
static void on_link_tap(const char *url) {
    do_navigate(url);
}
static void on_home() {
    do_navigate(HOMEPAGE);
}
static void update_nav_buttons() {
    header_set_back_enabled(history_can_back());
    header_set_forward_enabled(history_can_forward());
}

// Load a URL without modifying history — used by back/forward/retry
static void load_url(const char *url) {
    header_set_url(url);
    update_nav_buttons();
    if (lvgl_lock(50)) {
        page_show_spinner(s_content);
        header_set_loading(true);
        lvgl_unlock();
    }
    g_fetch_kb = 0;
    s_loading = true;
    net_task_load(url);
}

// Navigate to a new URL — pushes to history
static void do_navigate(const char *url) {
    history_push(url);
    load_url(url);
}

// Called from net task (core 0) — store result and notify core 1
static void on_page_ready(ParseResult *result, const char *url) {
    s_pending_result = result;
    if (s_ui_task_handle) xTaskNotifyGive(s_ui_task_handle);
}

static void on_form_submit(const char *action_url, bool is_post,
                            const char *encoded_body) {
    if (is_post) {
        // POST: need to pass body through net_task
        // For now, store body and navigate
        history_push(action_url);
        header_set_url(action_url);
        update_nav_buttons();
        if (lvgl_lock(50)) {
            page_show_spinner(s_content);
            header_set_loading(true);
            lvgl_unlock();
        }
        g_fetch_kb = 0;
        s_loading = true;
        net_task_load_post(action_url, encoded_body);
    } else {
        // GET: append query string to action URL
        char full_url[1024];
        const char *sep = strchr(action_url, '?') ? "&" : "?";
        snprintf(full_url, sizeof(full_url), "%s%s%s", action_url, sep, encoded_body);
        do_navigate(full_url);
    }
}

static void on_field_focus(lv_obj_t *textarea) {
    if (!s_kb || !s_content || !s_show_btn) return;
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_show_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(s_content, LV_VER_RES - 30 - KB_HEIGHT);
    lv_keyboard_set_textarea(s_kb, textarea);
    lv_textarea_set_cursor_pos(textarea, LV_TEXTAREA_CURSOR_LAST);
    lv_obj_add_state(textarea, LV_STATE_FOCUSED);
    s_kb_visible = true;
}

static void retry_btn_cb(lv_event_t *e) {
    retry_current();
}

static void retry_current() {
    const char *url = header_get_url_text();
    if (url && url[0]) load_url(url);
}

// --- Keyboard management ---

static void kb_value_changed_cb(lv_event_t *e) {
    uint16_t btn_id = lv_btnmatrix_get_selected_btn(s_kb);
    if (btn_id == LV_BTNMATRIX_BTN_NONE) return;
    const char *txt = lv_btnmatrix_get_btn_text(s_kb, btn_id);
    if (txt && strcmp(txt, LV_SYMBOL_UP) == 0) {
        // Default handler already typed the symbol — remove it
        lv_obj_t *ta = lv_keyboard_get_textarea(s_kb);
        if (ta) {
            size_t sym_len = strlen(LV_SYMBOL_UP);
            for (size_t i = 0; i < sym_len; i++) lv_textarea_del_char(ta);
        }
        lv_obj_scroll_to_y(s_content, 0, LV_ANIM_OFF);
    }
}

static void kb_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        lv_obj_t *ta = lv_keyboard_get_textarea(s_kb);
        if (ta == header_get_url_ta()) {
            // Enter on URL bar — navigate
            const char *url = header_get_url_text();
            if (url && url[0]) {
                kb_hide();
                do_navigate(url);
            }
        } else {
            // Enter on form field — submit the form
            kb_hide();
            if (s_cur_result && s_cur_result->form_action[0]) {
                static char form_data[4096];
                collect_form_data(s_content, s_cur_result,
                                  form_data, sizeof(form_data));
                on_form_submit(s_cur_result->form_action,
                               s_cur_result->form_is_post, form_data);
            }
        }
    } else if (code == LV_EVENT_CANCEL) {
        kb_hide();
    }
}

static void url_ta_click_cb(lv_event_t *e) {
    kb_show();
}

static void show_btn_cb(lv_event_t *e) {
    kb_show();
}

static void kb_show() {
    if (!s_kb || !s_content || !s_show_btn) return;
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_show_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(s_content, LV_VER_RES - 30 - KB_HEIGHT);
    lv_obj_t *ta = header_get_url_ta();
    lv_keyboard_set_textarea(s_kb, ta);
    lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);
    lv_obj_add_state(ta, LV_STATE_FOCUSED);
    s_kb_visible = true;
}

static void kb_hide() {
    if (!s_kb || !s_content || !s_show_btn) return;
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_show_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(s_content, LV_VER_RES - 30);
    lv_obj_clear_state(header_get_url_ta(), LV_STATE_FOCUSED);
    lv_keyboard_set_textarea(s_kb, NULL);
    s_kb_visible = false;
}

void ui_build_root() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A2E), 0);

    header_create(scr, do_navigate, on_back, on_forward, on_home);

    // Content area
    s_content = lv_obj_create(scr);
    lv_obj_set_size(s_content, LV_HOR_RES, LV_VER_RES - 30);
    lv_obj_set_pos(s_content, 0, 30);
    lv_obj_set_style_bg_color(s_content, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_border_width(s_content, 0, 0);
    lv_obj_set_style_radius(s_content, 0, 0);
    lv_obj_set_style_pad_all(s_content, 0, 0);
    lv_obj_set_style_shadow_width(s_content, 0, 0);
    lv_obj_set_style_outline_width(s_content, 0, 0);
    lv_obj_set_scroll_dir(s_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_content, LV_SCROLLBAR_MODE_AUTO);

    gesture_attach(s_content, on_back, on_forward);

    // Keyboard — hidden by default, minimal flat styling
    s_kb = lv_keyboard_create(scr);
    lv_obj_set_size(s_kb, LV_HOR_RES, KB_HEIGHT);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_gap(s_kb, 2, 0);
    // Remove theme — apply a bare style to all parts/states
    lv_obj_remove_style_all(s_kb);
    lv_obj_set_size(s_kb, LV_HOR_RES, KB_HEIGHT);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    // Main background
    lv_obj_set_style_bg_opa(s_kb, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_kb, lv_color_hex(0x16213E), 0);
    lv_obj_set_style_pad_all(s_kb, 2, 0);
    lv_obj_set_style_pad_gap(s_kb, 2, 0);
    // Keys — default
    lv_obj_set_style_bg_opa(s_kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_kb, lv_color_hex(0x0F3460), LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_kb, lv_color_hex(0xE0E0E0), LV_PART_ITEMS);
    lv_obj_set_style_text_font(s_kb, &lv_font_montserrat_18, LV_PART_ITEMS);
    // Keys — pressed
    lv_obj_set_style_bg_color(s_kb, lv_color_hex(0x4FC3F7), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(s_kb, lv_color_hex(0x1A1A2E), LV_PART_ITEMS | LV_STATE_PRESSED);
    // Keys — checked (Shift, ABC/123)
    lv_obj_set_style_bg_color(s_kb, lv_color_hex(0x0A2040), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(s_kb, lv_color_hex(0xE0E0E0), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    // Apply custom 5-row maps
    lv_keyboard_set_map(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER,
                        (const char **)kb_map_lc, kb_ctrl_lc);
    lv_keyboard_set_map(s_kb, LV_KEYBOARD_MODE_TEXT_UPPER,
                        (const char **)kb_map_uc, kb_ctrl_uc);
    lv_keyboard_set_map(s_kb, LV_KEYBOARD_MODE_SPECIAL,
                        (const char **)kb_map_spec, kb_ctrl_spec);
    lv_obj_add_event_cb(s_kb, kb_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_kb, kb_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_kb, kb_event_cb, LV_EVENT_CANCEL, NULL);

    // "Show" button — bottom-right, visible when keyboard hidden
    s_show_btn = lv_label_create(scr);
    lv_label_set_text(s_show_btn, "Kb");
    lv_obj_set_size(s_show_btn, 36, 24);
    lv_obj_align(s_show_btn, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
    lv_obj_set_style_bg_opa(s_show_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_show_btn, lv_color_hex(0x0F3460), 0);
    lv_obj_set_style_text_color(s_show_btn, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(s_show_btn, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(s_show_btn, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(s_show_btn, 4, 0);
    lv_obj_set_style_radius(s_show_btn, 2, 0);
    lv_obj_set_style_shadow_width(s_show_btn, 0, 0);
    lv_obj_set_style_border_width(s_show_btn, 0, 0);
    lv_obj_add_flag(s_show_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_show_btn, show_btn_cb, LV_EVENT_CLICKED, NULL);

    // Tap URL bar to show keyboard
    lv_obj_add_event_cb(header_get_url_ta(), url_ta_click_cb, LV_EVENT_CLICKED, NULL);

    update_nav_buttons();
}

static void ui_task_fn(void *arg) {
    dbglog_init();

    display_init();
    display_clear();
    lv_init();
    display_lvgl_init();
    touch_init();
    touch_lvgl_init();
    history_init();

    if (lvgl_lock(portMAX_DELAY)) {
        ui_build_root();
        lvgl_unlock();
    }

    net_task_start(on_page_ready);

    // Wait for WiFi before loading homepage
    while (WiFi.status() != WL_CONNECTED) {
        if (lvgl_lock(5)) {
            lv_obj_invalidate(lv_scr_act());
            lv_timer_handler();
            lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    do_navigate(HOMEPAGE);

    // LVGL tick loop
    for (;;) {
        if (lvgl_lock(5)) {
            // Check if net_task delivered a new page
            if (ulTaskNotifyTake(pdTRUE, 0)) {
                if (s_cur_result) parse_result_free(s_cur_result);
                s_cur_result = s_pending_result;
                s_pending_result = nullptr;
                header_set_loading(false);
                s_loading = false;
                ParseResult *result = s_cur_result;
                if (result && result->count > 0 && !result->error) {
                    page_render(s_content, result, on_link_tap,
                               on_form_submit, on_field_focus);
                } else {
                    page_clear(s_content);
                    lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
                    lv_obj_set_flex_align(s_content, LV_FLEX_ALIGN_CENTER,
                                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                    lv_obj_t *lbl = lv_label_create(s_content);
                    // Show error text from net_task (includes HTTP status if available)
                    const char *err_msg = (result && result->count > 0 && result->elems[0].text)
                                          ? result->elems[0].text : "Failed to load page.";
                    lv_label_set_text(lbl, err_msg);
                    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
                    // Retry button (flat label, clickable)
                    lv_obj_t *btn = lv_label_create(s_content);
                    lv_label_set_text(btn, "Retry");
                    lv_obj_set_size(btn, 80, 32);
                    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
                    lv_obj_set_style_bg_color(btn, lv_color_hex(0x0F3460), 0);
                    lv_obj_set_style_text_color(btn, lv_color_hex(0x4FC3F7), 0);
                    lv_obj_set_style_text_align(btn, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_pad_top(btn, 8, 0);
                    lv_obj_set_style_radius(btn, 0, 0);
                    lv_obj_set_style_shadow_width(btn, 0, 0);
                    lv_obj_set_style_border_width(btn, 0, 0);
                    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
                    lv_obj_add_event_cb(btn, retry_btn_cb, LV_EVENT_CLICKED, NULL);
                }
                lv_obj_scroll_to(s_content, 0, 0, LV_ANIM_OFF);
                update_nav_buttons();
            }
            // Update loading KB counter once per second
            if (s_loading) {
                static uint32_t last_kb_update = 0;
                static int last_kb_val = -1;
                int kb = g_fetch_kb;
                if (millis() - last_kb_update > 500) {
                    last_kb_update = millis();
                    last_kb_val = kb;
                    lv_obj_t *lbl = lv_obj_get_child(s_content, 0);
                    if (lbl) {
                        char buf[32];
                        if (kb > 0)
                            snprintf(buf, sizeof(buf), "Loading... %dKB", kb);
                        else
                            snprintf(buf, sizeof(buf), "Loading...");
                        lv_label_set_text(lbl, buf);
                    }
                }
            }
            // Periodic full invalidate — rounder_cb ensures all flushes are
            // sequential top-to-bottom, so this is safe at any interval
            static uint32_t last_inv = 0;
            if (millis() - last_inv > 100) {
                last_inv = millis();
                lv_obj_invalidate(lv_scr_act());
            }
            lv_timer_handler();
            lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void ui_task_start() {
    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    xTaskCreatePinnedToCore(ui_task_fn, "ui_task", 16384, nullptr, 5, &s_ui_task_handle, 1);
}
