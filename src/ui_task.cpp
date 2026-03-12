// 060326 Full browser UI wiring
// 110326 Replace diagnostic loop with full LVGL + browser init
// 120326 Fix cross-core rendering via task notification; sync flush + full invalidate
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

#define HOMEPAGE "https://en.wikipedia.org/wiki/ESP32"

static SemaphoreHandle_t s_lvgl_mutex    = nullptr;
static lv_obj_t         *s_content       = nullptr;
static ParseResult      *s_cur_result    = nullptr;
static ParseResult      *s_pending_result = nullptr;
static TaskHandle_t      s_ui_task_handle = nullptr;
volatile int             g_fetch_kb = 0;

bool lvgl_lock(uint32_t ms) {
    return xSemaphoreTakeRecursive(s_lvgl_mutex, pdMS_TO_TICKS(ms)) == pdTRUE;
}
void lvgl_unlock() {
    xSemaphoreGiveRecursive(s_lvgl_mutex);
}

static void do_navigate(const char *url);

static void on_back() {
    const char *url = history_back();
    if (url) do_navigate(url);
}
static void on_forward() {
    const char *url = history_forward();
    if (url) do_navigate(url);
}
static void on_link_tap(const char *url) {
    do_navigate(url);
}
static void update_nav_buttons() {
    header_set_back_enabled(history_can_back());
    header_set_forward_enabled(history_can_forward());
}

static void do_navigate(const char *url) {
    history_push(url);
    header_set_url(url);
    update_nav_buttons();
    if (lvgl_lock(50)) {
        page_show_spinner(s_content);
        header_set_loading(true);
        lvgl_unlock();
    }
    g_fetch_kb = 0;
    net_task_load(url);
}

// Called from net task (core 0) — store result and notify core 1
static void on_page_ready(ParseResult *result, const char *url) {
    s_pending_result = result;
    if (s_ui_task_handle) xTaskNotifyGive(s_ui_task_handle);
}

void ui_build_root() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A2E), 0);

    header_create(scr, do_navigate, on_back, on_forward);

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
                ParseResult *result = s_cur_result;
                if (result && result->count > 0) {
                    page_render(s_content, result, on_link_tap);
                } else {
                    page_clear(s_content);
                    lv_obj_t *lbl = lv_label_create(s_content);
                    lv_label_set_text(lbl, "Failed to load page.");
                    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
                    lv_obj_center(lbl);
                }
                lv_obj_scroll_to(s_content, 0, 0, LV_ANIM_OFF);
                update_nav_buttons();
            }
            // Periodic full invalidate — LVGL dirty tracking broken with this display
            // Only every 100ms to avoid starving touch input
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
