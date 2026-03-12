/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/**
 * @file
 * @brief ESP LCD & Touch: AXS15231B
 */

#pragma once

#include "esp_lcd_touch.h"
#include "esp_lcd_panel_vendor.h"

#define ESP_LCD_AXS15231B_VER_MAJOR    (1)
#define ESP_LCD_AXS15231B_VER_MINOR    (0)
#define ESP_LCD_AXS15231B_VER_PATCH    (0)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LCD panel initialization commands.
 *
 */
typedef struct {
    int cmd;                /*<! The specific LCD command */
    const void *data;       /*<! Buffer that holds the command specific data */
    size_t data_bytes;      /*<! Size of `data` in memory, in bytes */
    unsigned int delay_ms;  /*<! Delay in milliseconds after this command */
} axs15231b_lcd_init_cmd_t;

/**
 * @brief LCD panel vendor configuration.
 *
 */
typedef struct {
    const axs15231b_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
    struct {
        unsigned int use_qspi_interface: 1;
    } flags;
} axs15231b_vendor_config_t;

/**
 * @brief Create LCD panel for model AXS15231B
 */
esp_err_t esp_lcd_new_panel_axs15231b(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel);

/**
 * @brief LCD panel bus configuration macros
 */
#define AXS15231B_PANEL_BUS_SPI_CONFIG(sclk, mosi, max_trans_sz)  \
    {                                                           \
        .sclk_io_num = sclk,                                    \
        .mosi_io_num = mosi,                                    \
        .miso_io_num = -1,                                      \
        .quadhd_io_num = -1,                                    \
        .quadwp_io_num = -1,                                    \
        .max_transfer_sz = max_trans_sz,                        \
    }

#define AXS15231B_PANEL_BUS_QSPI_CONFIG(sclk, d0, d1, d2, d3, max_trans_sz) \
    {                                                           \
        .sclk_io_num = sclk,                                    \
        .data0_io_num = d0,                                     \
        .data1_io_num = d1,                                     \
        .data2_io_num = d2,                                     \
        .data3_io_num = d3,                                     \
        .max_transfer_sz = max_trans_sz,                        \
    }

/**
 * @brief LCD panel IO configuration macros
 */
#define AXS15231B_PANEL_IO_QSPI_CONFIG(cs, cb, cb_ctx)          \
    {                                                           \
        .cs_gpio_num = cs,                                      \
        .dc_gpio_num = -1,                                      \
        .spi_mode = 3,                                          \
        .pclk_hz = 40 * 1000 * 1000,                            \
        .trans_queue_depth = 10,                                \
        .on_color_trans_done = cb,                              \
        .user_ctx = cb_ctx,                                     \
        .lcd_cmd_bits = 32,                                     \
        .lcd_param_bits = 8,                                    \
        .flags = { .quad_mode = 1 },                            \
    }

/**
 * @brief Create a new AXS15231B touch driver
 */
esp_err_t esp_lcd_touch_new_i2c_axs15231b(const esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *tp);

#define ESP_LCD_TOUCH_IO_I2C_AXS15231B_ADDRESS    (0x3B)

#define ESP_LCD_TOUCH_IO_I2C_AXS15231B_CONFIG()             \
    {                                                       \
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_AXS15231B_ADDRESS, \
        .control_phase_bytes = 1,                           \
        .dc_bit_offset = 0,                                 \
        .lcd_cmd_bits = 8,                                  \
        .flags =                                            \
        {                                                   \
            .disable_control_phase = 1,                     \
        }                                                   \
    }

#ifdef __cplusplus
}
#endif
