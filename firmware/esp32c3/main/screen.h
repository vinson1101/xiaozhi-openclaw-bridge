#pragma once

#include <stdint.h>

#include "eyes.h"

#define XOB_SCREEN_WIDTH 240
#define XOB_SCREEN_HEIGHT 240
#define XOB_SCREEN_MAX_RECTS 160

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint16_t color;
} xob_screen_rect_t;

typedef struct {
    xob_screen_rect_t rects[XOB_SCREEN_MAX_RECTS];
    uint8_t count;
} xob_screen_frame_t;

xob_screen_frame_t xob_screen_render_eyes(const xob_eyes_frame_t *eyes);
