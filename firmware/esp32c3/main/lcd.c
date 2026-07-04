#include "lcd.h"

#include <stdbool.h>

#include "driver/ledc.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "xob_lcd";

#define XOB_LCD_HOST SPI2_HOST
#define XOB_LCD_PIN_MOSI 1
#define XOB_LCD_PIN_SCLK 3
#define XOB_LCD_PIN_CS 12
#define XOB_LCD_PIN_DC 0
#define XOB_LCD_PIN_RST 2
#define XOB_LCD_PIN_BL 5
#define XOB_LCD_PIXEL_CLOCK_HZ (80 * 1000 * 1000)
#define XOB_LCD_BACKLIGHT_PWM_HZ 25000
#define XOB_LCD_SWAP_XY true
#define XOB_LCD_MIRROR_X false
#define XOB_LCD_MIRROR_Y true
#define XOB_LCD_GAP_X 80
#define XOB_LCD_GAP_Y 0

static esp_lcd_panel_handle_t panel;
static bool panel_ready;

static esp_err_t init_backlight(void) {
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = XOB_LCD_BACKLIGHT_PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "ledc timer");

    const ledc_channel_config_t channel = {
        .gpio_num = XOB_LCD_PIN_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 1023,
        .hpoint = 0,
        .flags = {
            .output_invert = 0,
        },
    };
    return ledc_channel_config(&channel);
}

esp_err_t xob_lcd_init(void) {
    if (panel_ready) {
        return ESP_OK;
    }

    const spi_bus_config_t buscfg = {
        .mosi_io_num = XOB_LCD_PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = XOB_LCD_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = XOB_SCREEN_WIDTH * XOB_SCREEN_HEIGHT * sizeof(uint16_t),
    };
    esp_err_t err = spi_bus_initialize(XOB_LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(err, TAG, "spi bus init");
    }

    esp_lcd_panel_io_handle_t panel_io = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = XOB_LCD_PIN_CS,
        .dc_gpio_num = XOB_LCD_PIN_DC,
        .spi_mode = 3,
        .pclk_hz = XOB_LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)XOB_LCD_HOST, &io_config, &panel_io), TAG, "panel io");

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = XOB_LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel), TAG, "st7789 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "panel reset");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel, true), TAG, "panel invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(panel, XOB_LCD_SWAP_XY), TAG, "panel swap xy");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(panel, XOB_LCD_MIRROR_X, XOB_LCD_MIRROR_Y), TAG, "panel mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(panel, XOB_LCD_GAP_X, XOB_LCD_GAP_Y), TAG, "panel gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "panel on");
    ESP_RETURN_ON_ERROR(init_backlight(), TAG, "backlight");

    panel_ready = true;
    ESP_LOGI(TAG, "ST7789 ready: mosi=%d sclk=%d cs=%d dc=%d rst=%d bl=%d swap_xy=%d mirror_x=%d mirror_y=%d gap=%d,%d",
             XOB_LCD_PIN_MOSI, XOB_LCD_PIN_SCLK, XOB_LCD_PIN_CS, XOB_LCD_PIN_DC, XOB_LCD_PIN_RST, XOB_LCD_PIN_BL,
             XOB_LCD_SWAP_XY, XOB_LCD_MIRROR_X, XOB_LCD_MIRROR_Y, XOB_LCD_GAP_X, XOB_LCD_GAP_Y);
    return ESP_OK;
}

static esp_err_t draw_rect(const xob_screen_rect_t *rect) {
    static uint16_t line[XOB_SCREEN_WIDTH];
    uint16_t color = (uint16_t)((rect->color << 8) | (rect->color >> 8));

    for (int16_t x = 0; x < rect->w; ++x) {
        line[x] = color;
    }
    for (int16_t y = 0; y < rect->h; ++y) {
        ESP_RETURN_ON_ERROR(
            esp_lcd_panel_draw_bitmap(panel, rect->x, rect->y + y, rect->x + rect->w, rect->y + y + 1, line),
            TAG,
            "draw rect"
        );
    }
    return ESP_OK;
}

esp_err_t xob_lcd_draw_frame(const xob_screen_frame_t *frame) {
    if (!panel_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t i = 0; i < frame->count; ++i) {
        ESP_RETURN_ON_ERROR(draw_rect(&frame->rects[i]), TAG, "draw frame");
    }
    return ESP_OK;
}
