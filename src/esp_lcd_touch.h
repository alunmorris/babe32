/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include "sdkconfig.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_LCD_TOUCH_VER_MAJOR    (1)
#define ESP_LCD_TOUCH_VER_MINOR    (1)
#define ESP_LCD_TOUCH_VER_PATCH    (2)

#define CONFIG_ESP_LCD_TOUCH_MAX_POINTS     (1)
#define CONFIG_ESP_LCD_TOUCH_MAX_BUTTONS    (0)

typedef struct esp_lcd_touch_s esp_lcd_touch_t;
typedef esp_lcd_touch_t *esp_lcd_touch_handle_t;

typedef void (*esp_lcd_touch_interrupt_callback_t)(esp_lcd_touch_handle_t tp);

typedef struct {
    uint16_t x_max;
    uint16_t y_max;
    gpio_num_t rst_gpio_num;
    gpio_num_t int_gpio_num;
    struct {
        unsigned int reset: 1;
        unsigned int interrupt: 1;
    } levels;
    struct {
        unsigned int swap_xy: 1;
        unsigned int mirror_x: 1;
        unsigned int mirror_y: 1;
    } flags;
    void (*process_coordinates)(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num);
    esp_lcd_touch_interrupt_callback_t interrupt_callback;
    void *user_data;
    void *driver_data;
} esp_lcd_touch_config_t;

typedef struct {
    uint8_t points;
    struct {
        uint16_t x;
        uint16_t y;
        uint16_t strength;
    } coords[CONFIG_ESP_LCD_TOUCH_MAX_POINTS];
    portMUX_TYPE lock;
} esp_lcd_touch_data_t;

struct esp_lcd_touch_s {
    esp_err_t (*enter_sleep)(esp_lcd_touch_handle_t tp);
    esp_err_t (*exit_sleep)(esp_lcd_touch_handle_t tp);
    esp_err_t (*read_data)(esp_lcd_touch_handle_t tp);
    bool (*get_xy)(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num);
    esp_err_t (*set_swap_xy)(esp_lcd_touch_handle_t tp, bool swap);
    esp_err_t (*get_swap_xy)(esp_lcd_touch_handle_t tp, bool *swap);
    esp_err_t (*set_mirror_x)(esp_lcd_touch_handle_t tp, bool mirror);
    esp_err_t (*get_mirror_x)(esp_lcd_touch_handle_t tp, bool *mirror);
    esp_err_t (*set_mirror_y)(esp_lcd_touch_handle_t tp, bool mirror);
    esp_err_t (*get_mirror_y)(esp_lcd_touch_handle_t tp, bool *mirror);
    esp_err_t (*del)(esp_lcd_touch_handle_t tp);
    esp_lcd_touch_config_t config;
    esp_lcd_panel_io_handle_t io;
    esp_lcd_touch_data_t data;
};

esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t tp);
bool esp_lcd_touch_get_coordinates(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num);
esp_err_t esp_lcd_touch_set_swap_xy(esp_lcd_touch_handle_t tp, bool swap);
esp_err_t esp_lcd_touch_get_swap_xy(esp_lcd_touch_handle_t tp, bool *swap);
esp_err_t esp_lcd_touch_set_mirror_x(esp_lcd_touch_handle_t tp, bool mirror);
esp_err_t esp_lcd_touch_get_mirror_x(esp_lcd_touch_handle_t tp, bool *mirror);
esp_err_t esp_lcd_touch_set_mirror_y(esp_lcd_touch_handle_t tp, bool mirror);
esp_err_t esp_lcd_touch_get_mirror_y(esp_lcd_touch_handle_t tp, bool *mirror);
esp_err_t esp_lcd_touch_del(esp_lcd_touch_handle_t tp);
esp_err_t esp_lcd_touch_register_interrupt_callback(esp_lcd_touch_handle_t tp, esp_lcd_touch_interrupt_callback_t callback);
esp_err_t esp_lcd_touch_register_interrupt_callback_with_data(esp_lcd_touch_handle_t tp, esp_lcd_touch_interrupt_callback_t callback, void *user_data);
esp_err_t esp_lcd_touch_enter_sleep(esp_lcd_touch_handle_t tp);
esp_err_t esp_lcd_touch_exit_sleep(esp_lcd_touch_handle_t tp);

#ifdef __cplusplus
}
#endif
