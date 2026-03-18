// 180326 Boot menu and wiki search screens (extracted from ui_task.cpp)
#include "boot_menu.h"
#include "page_renderer.h"
#include "ui_header.h"
#include "ui_buttons.h"
#include "url_utils.h"
#include "wifi_mgr.h"
#include <Arduino.h>
#include <string.h>
#include <freertos/task.h>

extern bool lvgl_lock(uint32_t ms);
extern void lvgl_unlock();

#define HOMEPAGE "https://html.duckduckgo.com/lite"
#define AICHAT_URL "https://webmashing.com/aichat.php"

static lv_obj_t         *s_content    = nullptr;
static navigate_cb_t     s_nav_cb     = nullptr;
static field_focus_cb_t  s_focus_cb   = nullptr;
static urls_mode_cb_t    s_urls_cb    = nullptr;
static lv_obj_t         *s_wiki_ta    = nullptr;
static lv_obj_t         *s_inv_btn   = nullptr;
static lv_obj_t         *s_wifi_btn  = nullptr;
static volatile bool     s_portal_active = false;
static volatile bool     s_portal_done   = false;

struct MenuItem { const char *label; const char *url; };
static const MenuItem s_menu[] = {
    {"Search", HOMEPAGE},
    {"Wikipedia", nullptr},
    {"Hackaday", "https://hackaday.com"},
    {"AI Chat", AICHAT_URL},
    {"HTML test page", "https://www.wvc.edu/_resources/clutter/zz-test/_testing/test.aspx"},
};
static const int s_menu_count = sizeof(s_menu) / sizeof(s_menu[0]);

void boot_menu_init(lv_obj_t *content, navigate_cb_t nav,
                    field_focus_cb_t focus, urls_mode_cb_t urls_mode) {
    s_content  = content;
    s_nav_cb   = nav;
    s_focus_cb = focus;
    s_urls_cb  = urls_mode;
}

lv_obj_t *boot_menu_get_wiki_ta() {
    return s_wiki_ta;
}

static void hide_boot_buttons() {
    if (s_inv_btn) { lv_obj_del(s_inv_btn); s_inv_btn = nullptr; }
    if (s_wifi_btn) { lv_obj_del(s_wifi_btn); s_wifi_btn = nullptr; }
}

static void menu_item_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < s_menu_count) {
        hide_boot_buttons();
        const char *label = s_menu[idx].label;
        if (strcmp(label, "Search") == 0 || strcmp(label, "Wikipedia") == 0)
            if (s_urls_cb) s_urls_cb();
        if (s_menu[idx].url) {
            if (s_nav_cb) s_nav_cb(s_menu[idx].url);
        } else if (strcmp(label, "Wikipedia") == 0) {
            show_wiki_search();
        }
    }
}

static void wiki_search_cb(lv_event_t *e) {
    if (!s_wiki_ta) return;
    const char *text = lv_textarea_get_text(s_wiki_ta);
    if (!text || !text[0]) return;

    char encoded[512];
    url_encode(text, encoded, sizeof(encoded));

    char url[768];
    snprintf(url, sizeof(url),
             "https://en.wikipedia.org/w/index.php?search=%s", encoded);
    if (s_nav_cb) s_nav_cb(url);
}

void boot_menu_wiki_submit() {
    wiki_search_cb(nullptr);
}

void show_wiki_search() {
    if (!lvgl_lock(50)) return;
    header_set_visible(true);
    lv_obj_set_pos(s_content, 0, 30);
    lv_obj_set_height(s_content, LV_VER_RES - 30);
    page_clear(s_content);
    lv_obj_add_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_content, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Title
    lv_obj_t *title = lv_label_create(s_content);
    lv_label_set_text(title, "Wikipedia Search");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00C853), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_pad_bottom(title, 12, 0);

    // Search input
    s_wiki_ta = lv_textarea_create(s_content);
    lv_obj_t *ta = s_wiki_ta;
    lv_obj_set_width(ta, LV_PCT(80));
    lv_obj_set_height(ta, 32);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, "Search Wikipedia...");
    bool inv = page_is_inverted();
    lv_obj_set_style_bg_color(ta, lv_color_hex(inv ? 0xFFFFFF : 0x16213E), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(inv ? 0x1A1A1A : 0xE0E0E0), 0);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_14, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(inv ? 0xCCCCCC : 0x0F3460), 0);
    lv_obj_set_style_radius(ta, 0, 0);
    lv_obj_set_style_shadow_width(ta, 0, 0);
    lv_obj_set_style_pad_all(ta, 4, 0);

    // Search button
    lv_obj_t *btn = create_flat_btn(s_content, "Search", 80, 32,
                                     wiki_search_cb, ta);
    if (inv) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xE0E0E0), 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(0x0066CC), 0);
    }

    // Tap textarea to show keyboard
    lv_obj_add_event_cb(ta, [](lv_event_t *e) {
        if (s_focus_cb) s_focus_cb(lv_event_get_target(e));
    }, LV_EVENT_CLICKED, NULL);

    header_set_url("");
    lvgl_unlock();
}

static void wifi_portal_task(void *arg) {
    wifi_mgr_start_portal();
    s_portal_active = false;
    s_portal_done = true;
    vTaskDelete(nullptr);
}

static void wifi_setup_cb(lv_event_t *e) {
    if (s_portal_active) return;
    s_portal_active = true;
    // Show message on screen
    if (lvgl_lock(50)) {
        hide_boot_buttons();
        if (s_wifi_btn) { lv_obj_del(s_wifi_btn); s_wifi_btn = nullptr; }
        page_clear(s_content);
        lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(s_content, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        bool inv = page_is_inverted();
        lv_obj_t *lbl = lv_label_create(s_content);
        lv_label_set_text(lbl, "WiFi Setup Active\n\n"
            "Connect to AP:\n\"ESP32-Browser\"\n\n"
            "Then open 192.168.4.1\nin your browser");
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(lbl, LV_PCT(80));
        lv_obj_set_style_text_color(lbl, lv_color_hex(inv ? 0x000000 : 0xFFFFFF), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lvgl_unlock();
    }
    // Run portal on a separate task (it blocks)
    xTaskCreatePinnedToCore(wifi_portal_task, "wifi_portal", 8192,
                            nullptr, 2, nullptr, 0);
}

extern const lv_img_dsc_t babe32_img;

void show_boot_menu() {
    if (!lvgl_lock(50)) return;
    header_set_visible(true);
    lv_obj_set_pos(s_content, 0, 30);
    lv_obj_set_height(s_content, LV_VER_RES - 30);
    page_clear(s_content);
    s_wiki_ta = nullptr;

    bool inv = page_is_inverted();
    lv_color_t bg = lv_color_hex(inv ? 0xF0F0F0 : 0x1A1A2E);

    lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_content, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_color(s_content, bg, 0);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);

    // Image — bottom-left
    lv_obj_t *pig = lv_img_create(s_content);
    lv_img_set_src(pig, &babe32_img);
    lv_obj_clear_flag(pig, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pig, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_pos(pig, -lv_obj_get_style_pad_left(s_content, 0),
                        lv_obj_get_height(s_content) - 290
                        - lv_obj_get_style_pad_top(s_content, 0));

    // Right column for title + menu
    int col_x = 165;
    int col_w = LV_HOR_RES - col_x;

    // Title
    lv_obj_t *title = lv_label_create(s_content);
    lv_label_set_text(title, "Barely Adequate Browser ESP32");
    lv_obj_set_width(title, col_w);
    lv_obj_set_style_text_color(title,
        lv_color_hex(inv ? 0x000000 : 0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_pad_bottom(title, 12, 0);
    lv_obj_add_flag(title, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_width(title, LV_HOR_RES);
    lv_obj_set_pos(title, 0, 8);

    // Menu items
    int y_pos = 80;
    for (int i = 0; i < s_menu_count; i++) {
        lv_obj_t *item = lv_label_create(s_content);
        lv_label_set_text(item, s_menu[i].label);
        lv_obj_set_width(item, col_w);
        lv_obj_set_style_text_color(item,
            lv_color_hex(inv ? 0x0066CC : 0x4FC3F7), 0);
        lv_obj_set_style_text_font(item, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_align(item, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_all(item, 6, 0);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_FLOATING);
        lv_obj_set_pos(item, col_x, y_pos);
        lv_obj_add_event_cb(item, menu_item_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        y_pos += 30;
    }

    // Light/Dark mode toggle button (narrower) + WiFi Setup button
    int btn_w = 90;
    int btn_gap = 8;
    int total_w = btn_w * 2 + btn_gap;
    int btn_x = col_x + (col_w - total_w) / 2;
    int btn_y = LV_VER_RES - 36;

    if (s_inv_btn) { lv_obj_del(s_inv_btn); s_inv_btn = nullptr; }
    s_inv_btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(s_inv_btn, btn_w, 32);
    lv_obj_set_pos(s_inv_btn, btn_x, btn_y);
    lv_obj_set_style_bg_color(s_inv_btn, lv_color_hex(inv ? 0xCCCCCC : 0x0F3460), 0);
    lv_obj_set_style_radius(s_inv_btn, 4, 0);
    lv_obj_set_style_shadow_width(s_inv_btn, 0, 0);
    lv_obj_t *inv_lbl = lv_label_create(s_inv_btn);
    lv_label_set_text(inv_lbl, inv ? "Dark" : "Light");
    lv_obj_set_style_text_color(inv_lbl, lv_color_hex(inv ? 0x333333 : 0xCCCCCC), 0);
    lv_obj_set_style_text_font(inv_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(inv_lbl);
    lv_obj_add_event_cb(s_inv_btn, [](lv_event_t *e) {
        lv_obj_del(lv_event_get_target(e));
        s_inv_btn = nullptr;
        if (s_wifi_btn) { lv_obj_del(s_wifi_btn); s_wifi_btn = nullptr; }
        page_set_inverted(!page_is_inverted());
        lv_obj_set_style_bg_color(lv_scr_act(),
            lv_color_hex(page_is_inverted() ? 0xF0F0F0 : 0x1A1A2E), 0);
        show_boot_menu();
    }, LV_EVENT_CLICKED, nullptr);

    // WiFi Setup button
    if (s_wifi_btn) { lv_obj_del(s_wifi_btn); s_wifi_btn = nullptr; }
    s_wifi_btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(s_wifi_btn, btn_w, 32);
    lv_obj_set_pos(s_wifi_btn, btn_x + btn_w + btn_gap, btn_y);
    lv_obj_set_style_bg_color(s_wifi_btn, lv_color_hex(inv ? 0xCCCCCC : 0x0F3460), 0);
    lv_obj_set_style_radius(s_wifi_btn, 4, 0);
    lv_obj_set_style_shadow_width(s_wifi_btn, 0, 0);
    lv_obj_t *wifi_lbl = lv_label_create(s_wifi_btn);
    lv_label_set_text(wifi_lbl, "WiFi");
    lv_obj_set_style_text_color(wifi_lbl, lv_color_hex(inv ? 0x333333 : 0xCCCCCC), 0);
    lv_obj_set_style_text_font(wifi_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(wifi_lbl);
    lv_obj_add_event_cb(s_wifi_btn, wifi_setup_cb, LV_EVENT_CLICKED, nullptr);

    header_set_url("");
    lvgl_unlock();
}

bool boot_menu_portal_check() {
    if (s_portal_done) {
        s_portal_done = false;
        show_boot_menu();
        return true;
    }
    return false;
}
