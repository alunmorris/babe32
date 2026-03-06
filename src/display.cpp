// 060326 ST7701S: SPI init commands + RGB panel via esp_lcd
#include "display.h"
#include <Arduino.h>
#include <SPI.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_panel_ops.h>

// ---- Pin definitions ----
#define LCD_BL      38
#define LCD_SPI_CS  39
#define LCD_SPI_SCK 48
#define LCD_SPI_MOSI 47

#define LCD_DE    18
#define LCD_VSYNC 17
#define LCD_HSYNC 16
#define LCD_PCLK  21

// RGB data pins: R[4:0], G[5:0], B[4:0]
static const int LCD_R[] = {4, 5, 6, 7, 15};
static const int LCD_G[] = {8, 20, 3, 46, 9, 10};
static const int LCD_B[] = {11, 12, 13, 14, 0};

#define LCD_H_RES 480
#define LCD_V_RES 480

// Full-screen LVGL draw buffer in PSRAM.
// Single full-screen buffer + full_refresh=1 means one flush per frame,
// avoiding the strip-by-strip racing with the RGB panel scanner.
static lv_color_t *buf1 = nullptr;
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;

static esp_lcd_panel_handle_t panel_handle = nullptr;

// ---- ST7701S SPI init sequence ----
// Send a command byte via software SPI
static void st7701_spi_write(uint8_t cmd, const uint8_t *data, size_t len) {
    digitalWrite(LCD_SPI_CS, LOW);
    // Command byte: DC=0 => first bit 0, then 8-bit cmd
    // ST7701S uses 9-bit SPI: first bit = DC (0=cmd,1=data)
    uint16_t word = (0 << 8) | cmd;
    for (int i = 8; i >= 0; i--) {
        digitalWrite(LCD_SPI_SCK, LOW);
        digitalWrite(LCD_SPI_MOSI, (word >> i) & 1);
        digitalWrite(LCD_SPI_SCK, HIGH);
    }
    for (size_t b = 0; b < len; b++) {
        word = (1 << 8) | data[b];
        for (int i = 8; i >= 0; i--) {
            digitalWrite(LCD_SPI_SCK, LOW);
            digitalWrite(LCD_SPI_MOSI, (word >> i) & 1);
            digitalWrite(LCD_SPI_SCK, HIGH);
        }
    }
    digitalWrite(LCD_SPI_CS, HIGH);
}

static void st7701_init_sequence() {
    // Standard ST7701S init for 480x480
    uint8_t d[16];

    // CND2BKxSEL: switch to page 1
    d[0]=0x77; d[1]=0x01; d[2]=0x00; d[3]=0x00; d[4]=0x10;
    st7701_spi_write(0xFF, d, 5);

    d[0]=0xE0; st7701_spi_write(0xC0, d, 1); // LNESET
    d[0]=0x03; st7701_spi_write(0xC1, d, 1);
    d[0]=0x02; d[1]=0x02; st7701_spi_write(0xC2, d, 2);
    // VGHS, VGLS
    d[0]=0xBB; st7701_spi_write(0xCC, d, 1);
    // Gamma B0
    uint8_t gamma_pos[] = {0x00,0x0D,0x14,0x0D,0x10,0x05,0x02,0x08,
                           0x08,0x1E,0x05,0x13,0x11,0x28,0x32,0x1C};
    st7701_spi_write(0xB0, gamma_pos, 16);
    // Gamma B1
    uint8_t gamma_neg[] = {0x00,0x0D,0x14,0x0C,0x10,0x05,0x02,0x08,
                           0x07,0x1E,0x04,0x13,0x11,0x28,0x32,0x1C};
    st7701_spi_write(0xB1, gamma_neg, 16);

    // Back to page 0
    d[0]=0x77; d[1]=0x01; d[2]=0x00; d[3]=0x00; d[4]=0x00;
    st7701_spi_write(0xFF, d, 5);

    // COLMOD: 16bpp RGB565
    d[0]=0x55; st7701_spi_write(0x3A, d, 1);
    // MADCTL
    d[0]=0x00; st7701_spi_write(0x36, d, 1);

    // Sleep out
    st7701_spi_write(0x11, nullptr, 0);
    delay(120);
    // Display on
    st7701_spi_write(0x29, nullptr, 0);
    delay(20);
}

// ---- LVGL flush callback ----
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map) {
    esp_lcd_panel_draw_bitmap(panel_handle,
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1,
                              color_map);
    lv_disp_flush_ready(drv);
}

void display_init() {
    // Backlight off during init
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, LOW);

    // SPI pins for ST7701S init
    pinMode(LCD_SPI_CS, OUTPUT);
    pinMode(LCD_SPI_SCK, OUTPUT);
    pinMode(LCD_SPI_MOSI, OUTPUT);
    digitalWrite(LCD_SPI_CS, HIGH);
    digitalWrite(LCD_SPI_SCK, LOW);

    // Run ST7701S init sequence
    st7701_init_sequence();

    // Configure RGB panel
    esp_lcd_rgb_panel_config_t panel_config = {};
    panel_config.clk_src = LCD_CLK_SRC_PLL160M;
    panel_config.timings.h_res = LCD_H_RES;
    panel_config.timings.v_res = LCD_V_RES;
    panel_config.timings.pclk_hz = 12 * 1000 * 1000; // 12MHz — 16MHz causes bit errors on fast transitions
    panel_config.timings.hsync_back_porch = 10;
    panel_config.timings.hsync_front_porch = 50;
    panel_config.timings.hsync_pulse_width = 8;
    panel_config.timings.vsync_back_porch = 10;
    panel_config.timings.vsync_front_porch = 20;
    panel_config.timings.vsync_pulse_width = 8;
    panel_config.timings.flags.pclk_active_neg = 1;
    panel_config.data_width = 16; // RGB565
    panel_config.psram_trans_align = 64;

    // RGB data pins flat array: R4..R0, G5..G0, B4..B0
    int data_pins[] = {LCD_R[4],LCD_R[3],LCD_R[2],LCD_R[1],LCD_R[0],
                       LCD_G[5],LCD_G[4],LCD_G[3],LCD_G[2],LCD_G[1],LCD_G[0],
                       LCD_B[4],LCD_B[3],LCD_B[2],LCD_B[1],LCD_B[0]};
    memcpy(panel_config.data_gpio_nums, data_pins, sizeof(data_pins));

    panel_config.hsync_gpio_num = LCD_HSYNC;
    panel_config.vsync_gpio_num = LCD_VSYNC;
    panel_config.de_gpio_num    = LCD_DE;
    panel_config.pclk_gpio_num  = LCD_PCLK;
    panel_config.disp_gpio_num  = GPIO_NUM_NC;
    panel_config.flags.fb_in_psram = 1;

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    // Backlight on
    digitalWrite(LCD_BL, HIGH);
    Serial.println("Display initialised");
}

void display_lvgl_init() {
    // Full-screen draw buffer in PSRAM (480*480*2 = 460 KB)
    size_t buf_size = LCD_H_RES * LCD_V_RES * sizeof(lv_color_t);
    buf1 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    assert(buf1);

    lv_disp_draw_buf_init(&draw_buf, buf1, nullptr, LCD_H_RES * LCD_V_RES);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = LCD_H_RES;
    disp_drv.ver_res      = LCD_V_RES;
    disp_drv.flush_cb     = lvgl_flush_cb;
    disp_drv.draw_buf     = &draw_buf;
    disp_drv.full_refresh = 1;  // single flush per frame, avoids strip-scan race
    lv_disp_drv_register(&disp_drv);
    Serial.println("LVGL display driver registered");
}
