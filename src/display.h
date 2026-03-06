// 060326 ST7701S RGB panel driver with LVGL flush callback
#pragma once
#include <lvgl.h>

void display_init();
// Called after display_init() — registers LVGL display driver
void display_lvgl_init();
