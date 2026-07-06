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

static uint16_t lcd_color(uint16_t rgb565) {
    return (uint16_t)((rgb565 << 8) | (rgb565 >> 8));
}

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
    uint16_t color = lcd_color(rect->color);

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

static uint32_t next_utf8_codepoint(const char **cursor) {
    const unsigned char *p = (const unsigned char *)*cursor;
    if (*p == '\0') {
        return 0;
    }
    if (*p < 0x80) {
        *cursor = (const char *)(p + 1);
        return *p;
    }
    if ((*p & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80) {
        *cursor = (const char *)(p + 2);
        return ((uint32_t)(p[0] & 0x1f) << 6) | (uint32_t)(p[1] & 0x3f);
    }
    if ((*p & 0xf0) == 0xe0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) {
        *cursor = (const char *)(p + 3);
        return ((uint32_t)(p[0] & 0x0f) << 12) |
               ((uint32_t)(p[1] & 0x3f) << 6) |
               (uint32_t)(p[2] & 0x3f);
    }
    if ((*p & 0xf8) == 0xf0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80 && (p[3] & 0xc0) == 0x80) {
        *cursor = (const char *)(p + 4);
        return ((uint32_t)(p[0] & 0x07) << 18) |
               ((uint32_t)(p[1] & 0x3f) << 12) |
               ((uint32_t)(p[2] & 0x3f) << 6) |
               (uint32_t)(p[3] & 0x3f);
    }
    *cursor = (const char *)(p + 1);
    return '?';
}

static const uint8_t *ascii_glyph(char c) {
    static const uint8_t blank[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t question[5] = {0x02, 0x01, 0x51, 0x09, 0x06};
    static const uint8_t punct[][5] = {
        {0x00, 0x00, 0x5f, 0x00, 0x00}, /* ! */
        {0x00, 0x60, 0x60, 0x00, 0x00}, /* . */
        {0x00, 0x40, 0x30, 0x00, 0x00}, /* , */
        {0x08, 0x08, 0x08, 0x08, 0x08}, /* - */
        {0x20, 0x10, 0x08, 0x04, 0x02}, /* / */
        {0x00, 0x36, 0x36, 0x00, 0x00}, /* : */
    };
    static const uint8_t digits[10][5] = {
        {0x3e, 0x51, 0x49, 0x45, 0x3e},
        {0x00, 0x42, 0x7f, 0x40, 0x00},
        {0x42, 0x61, 0x51, 0x49, 0x46},
        {0x21, 0x41, 0x45, 0x4b, 0x31},
        {0x18, 0x14, 0x12, 0x7f, 0x10},
        {0x27, 0x45, 0x45, 0x45, 0x39},
        {0x3c, 0x4a, 0x49, 0x49, 0x30},
        {0x01, 0x71, 0x09, 0x05, 0x03},
        {0x36, 0x49, 0x49, 0x49, 0x36},
        {0x06, 0x49, 0x49, 0x29, 0x1e},
    };
    static const uint8_t upper[26][5] = {
        {0x7e, 0x11, 0x11, 0x11, 0x7e},
        {0x7f, 0x49, 0x49, 0x49, 0x36},
        {0x3e, 0x41, 0x41, 0x41, 0x22},
        {0x7f, 0x41, 0x41, 0x22, 0x1c},
        {0x7f, 0x49, 0x49, 0x49, 0x41},
        {0x7f, 0x09, 0x09, 0x09, 0x01},
        {0x3e, 0x41, 0x49, 0x49, 0x7a},
        {0x7f, 0x08, 0x08, 0x08, 0x7f},
        {0x00, 0x41, 0x7f, 0x41, 0x00},
        {0x20, 0x40, 0x41, 0x3f, 0x01},
        {0x7f, 0x08, 0x14, 0x22, 0x41},
        {0x7f, 0x40, 0x40, 0x40, 0x40},
        {0x7f, 0x02, 0x0c, 0x02, 0x7f},
        {0x7f, 0x04, 0x08, 0x10, 0x7f},
        {0x3e, 0x41, 0x41, 0x41, 0x3e},
        {0x7f, 0x09, 0x09, 0x09, 0x06},
        {0x3e, 0x41, 0x51, 0x21, 0x5e},
        {0x7f, 0x09, 0x19, 0x29, 0x46},
        {0x46, 0x49, 0x49, 0x49, 0x31},
        {0x01, 0x01, 0x7f, 0x01, 0x01},
        {0x3f, 0x40, 0x40, 0x40, 0x3f},
        {0x1f, 0x20, 0x40, 0x20, 0x1f},
        {0x3f, 0x40, 0x38, 0x40, 0x3f},
        {0x63, 0x14, 0x08, 0x14, 0x63},
        {0x07, 0x08, 0x70, 0x08, 0x07},
        {0x61, 0x51, 0x49, 0x45, 0x43},
    };

    if (c == ' ') {
        return blank;
    }
    if (c >= '0' && c <= '9') {
        return digits[c - '0'];
    }
    if (c >= 'A' && c <= 'Z') {
        return upper[c - 'A'];
    }
    switch (c) {
        case '!':
            return punct[0];
        case '.':
            return punct[1];
        case ',':
            return punct[2];
        case '-':
            return punct[3];
        case '/':
            return punct[4];
        case ':':
            return punct[5];
        default:
            return question;
    }
}

static char display_char(uint32_t codepoint) {
    if (codepoint >= 'a' && codepoint <= 'z') {
        return (char)(codepoint - 32);
    }
    if (codepoint >= 0x20 && codepoint <= 0x7e) {
        return (char)codepoint;
    }
    return '?';
}

static int16_t text_width(const char *text) {
    int16_t width = 0;
    const char *cursor = text;
    while (text != NULL && *cursor != '\0') {
        (void)display_char(next_utf8_codepoint(&cursor));
        if (width < 3000) {
            width = (int16_t)(width + 12);
        }
    }
    return width;
}

static bool text_is_displayable(const char *text) {
    while (text != NULL && *text != '\0') {
        if ((unsigned char)*text >= 0x80) {
            return false;
        }
        text++;
    }
    return true;
}

static esp_err_t draw_ascii_char(char c, int16_t x, int16_t y, uint16_t color) {
    static uint16_t line[10];
    const uint8_t *glyph = ascii_glyph(c);
    uint16_t fg = lcd_color(color);
    uint16_t bg = lcd_color(0x0000);

    for (int16_t row = 0; row < 14; row++) {
        int16_t screen_y = (int16_t)(y + row);
        if (screen_y < 0 || screen_y >= XOB_SCREEN_HEIGHT) {
            continue;
        }
        int16_t clip_x0 = x < 0 ? 0 : x;
        int16_t clip_x1 = x + 10 > XOB_SCREEN_WIDTH ? XOB_SCREEN_WIDTH : (int16_t)(x + 10);
        if (clip_x1 <= clip_x0) {
            continue;
        }
        for (int16_t x_pos = clip_x0; x_pos < clip_x1; x_pos++) {
            int16_t col = (int16_t)((x_pos - x) / 2);
            bool on = (glyph[col] & (1u << (row / 2))) != 0;
            line[x_pos - clip_x0] = on ? fg : bg;
        }
        ESP_RETURN_ON_ERROR(
            esp_lcd_panel_draw_bitmap(panel, clip_x0, screen_y, clip_x1, screen_y + 1, line),
            TAG,
            "draw ascii"
        );
    }
    return ESP_OK;
}

static esp_err_t draw_text_line(const char *text, int16_t x, int16_t y, uint16_t color) {
    if (text == NULL) {
        return ESP_OK;
    }
    int16_t cursor_x = x;
    const char *cursor = text;
    while (*cursor != '\0') {
        char c = display_char(next_utf8_codepoint(&cursor));
        if (cursor_x > XOB_SCREEN_WIDTH) {
            break;
        }
        ESP_RETURN_ON_ERROR(draw_ascii_char(c, cursor_x, y, color), TAG, "draw text");
        cursor_x = (int16_t)(cursor_x + 12);
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

esp_err_t xob_lcd_draw_dialog_text(const char *status, const char *input, const char *output, uint16_t scroll_step) {
    if (!panel_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    const char *status_text = status && status[0] ? status : "IDLE";
    int16_t status_x = (int16_t)(XOB_SCREEN_WIDTH - text_width(status_text) - 8);
    if (status_x < 70) {
        status_x = 70;
    }
    ESP_RETURN_ON_ERROR(draw_text_line(status_text, status_x, 8, 0xffff), TAG, "draw status");

    const char *text = output && output[0] ? output : input && input[0] ? input : "";
    if (!text_is_displayable(text)) {
        text = "";
    }
    int16_t width = text_width(text);
    int16_t x = 8;
    if (width > XOB_SCREEN_WIDTH - 16) {
        int16_t gap = 32;
        int16_t offset = (int16_t)((scroll_step * 4) % (width + gap));
        x = (int16_t)(8 - offset);
        ESP_RETURN_ON_ERROR(draw_text_line(text, (int16_t)(x + width + gap), 212, 0x4208), TAG, "draw marquee tail");
    }
    return draw_text_line(text, x, 212, 0xffff);
}
