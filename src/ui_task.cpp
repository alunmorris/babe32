// 060326 FreeRTOS task: runs lv_timer_handler on core 1
#include "ui_task.h"
#include <Arduino.h>
#include <lvgl.h>
#include "display.h"
#include "touch.h"

static SemaphoreHandle_t lvgl_mutex;

bool lvgl_lock(uint32_t timeout_ms) {
    return xSemaphoreTakeRecursive(lvgl_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}
void lvgl_unlock() {
    xSemaphoreGiveRecursive(lvgl_mutex);
}

static void ui_task_fn(void *arg) {
    lv_init();
    display_init();
    display_lvgl_init();
    touch_init();
    touch_lvgl_init();

    ui_build_root();

    for (;;) {
        if (lvgl_lock(10)) {
            lv_timer_handler();
            lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void ui_task_start() {
    lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    xTaskCreatePinnedToCore(ui_task_fn, "ui_task", 8192, nullptr, 5, nullptr, 1);
}

void ui_build_root() {
    // Placeholder — Task 14 replaces this
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "UI Task Running");
    lv_obj_center(label);
}
