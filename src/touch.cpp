// 060326 GT911 on I2C SDA=19 SCL=45, addr auto-detected by bb_captouch
#include "touch.h"
#include <Arduino.h>
#include <bb_captouch.h>

#define TOUCH_SDA 19
#define TOUCH_SCL 45

static BBCapTouch touch;
static lv_indev_drv_t indev_drv;

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    TOUCHINFO ti;
    if (touch.getSamples(&ti) && ti.count > 0) {
        data->point.x = ti.x[0];
        data->point.y = ti.y[0];
        data->state   = LV_INDEV_STATE_PR;
        Serial.printf("Touch PRESS  x=%d y=%d\n", ti.x[0], ti.y[0]);
    } else {
        if (data->state == LV_INDEV_STATE_PR) {
            Serial.println("Touch RELEASE");
        }
        data->state = LV_INDEV_STATE_REL;
    }
}

void touch_init() {
    if (!touch.init(TOUCH_SDA, TOUCH_SCL, -1, -1)) {  // -1 = RST/INT not used
        Serial.println("ERROR: Touch init failed");
        return;
    }
    Serial.println("Touch initialised (GT911)");
}

void touch_lvgl_init() {
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&indev_drv);
    Serial.println("LVGL touch indev registered");
}
