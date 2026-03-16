#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Resolution (informational — actual res set via display driver in LVGL 8) */
#define LV_HOR_RES_MAX  480
#define LV_VER_RES_MAX  320
/* Use int32_t coordinates to avoid overflow on long pages (default int16_t caps at 32767px) */
#define LV_USE_LARGE_COORD  1

#define LV_COLOR_DEPTH       16
#define LV_COLOR_16_SWAP     1   /* required for SPI: ESP32 is little-endian, display expects big-endian */

/* PSRAM custom allocator — macro form required by LVGL 8 */
#define LV_MEM_CUSTOM   1
#define LV_MEM_CUSTOM_INCLUDE  <esp_heap_caps.h>
#define LV_MEM_CUSTOM_ALLOC(size)   heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LV_MEM_CUSTOM_FREE          free
#define LV_MEM_CUSTOM_REALLOC(p, size) heap_caps_realloc((p), (size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

/* Enable logging */
#define LV_USE_LOG      1
#define LV_LOG_LEVEL    LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF   1

/* Tick */
#define LV_TICK_CUSTOM  1
#define LV_TICK_CUSTOM_INCLUDE  <Arduino.h>
#define LV_TICK_CUSTOM_SYS_TIME_EXPR  (millis())

/* Built-in fonts — we need multiple sizes */
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_18  1
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_MONTSERRAT_24  1
#define LV_FONT_DEFAULT        &lv_font_montserrat_14

/* Widgets needed */
#define LV_USE_LABEL    1
#define LV_USE_BTN      1
#define LV_USE_IMG      1
#define LV_USE_TEXTAREA 1
#define LV_USE_KEYBOARD 1
#define LV_USE_DROPDOWN 1
#define LV_USE_SPINNER  1
#define LV_USE_MSGBOX   1

/* Image decoders */
#define LV_USE_PNG      1
#define LV_USE_SJPG     1

/* Animation (LVGL 8 name) */
#define LV_USE_ANIM     1

/* Input device */
#define LV_USE_INDEV    1

#endif /* LV_CONF_H */
