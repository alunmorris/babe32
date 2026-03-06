// 060326 Task 3: test display + touch
// NOTE: Touch (GPIO19/SDA) conflicts with USB serial when connected.
// Touch will work in final firmware without USB active.
#include <Arduino.h>
#include <lvgl.h>
#include "display.h"
#include "touch.h"

static void btn_cb(lv_event_t *e) {
    Serial.println("Button tapped!");
}

void setup() {
    Serial.begin(115200);
    delay(500);

    lv_init();
    display_init();
    display_lvgl_init();
    touch_init();
    touch_lvgl_init();

    lv_obj_t *btn = lv_btn_create(lv_scr_act());
    lv_obj_center(btn);
    lv_obj_set_size(btn, 200, 80);
    lv_obj_add_event_cb(btn, btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Tap me");
    lv_obj_center(lbl);
}

void loop() {
    lv_timer_handler();
    delay(5);
}
