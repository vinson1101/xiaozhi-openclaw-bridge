#pragma once

#include "esp_err.h"

#include "screen.h"

esp_err_t xob_lcd_init(void);
esp_err_t xob_lcd_draw_frame(const xob_screen_frame_t *frame);
esp_err_t xob_lcd_draw_dialog_text(const char *status, const char *input, const char *output, uint16_t scroll_step);
