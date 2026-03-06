// 060326 Task 2: test display
#include <Arduino.h>
#include <lvgl.h>
#include "display.h"

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("Boot");

    lv_init();
    display_init();
    display_lvgl_init();

    // Test: fill screen blue
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0000FF), 0);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Display OK");
    lv_obj_center(label);
}

void loop() {
    lv_timer_handler();
    delay(5);
}
