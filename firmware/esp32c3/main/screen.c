#include "screen.h"

#define XOB_RGB565_BLACK 0x0000
#define XOB_RGB565_WHITE 0xffff

static int16_t clamp_i16(int16_t value, int16_t min_value, int16_t max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void add_rect(xob_screen_frame_t *frame, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    int16_t x1 = clamp_i16(x, 0, XOB_SCREEN_WIDTH);
    int16_t y1 = clamp_i16(y, 0, XOB_SCREEN_HEIGHT);
    int16_t x2 = clamp_i16((int16_t)(x + w), 0, XOB_SCREEN_WIDTH);
    int16_t y2 = clamp_i16((int16_t)(y + h), 0, XOB_SCREEN_HEIGHT);

    if (x2 <= x1 || y2 <= y1 || frame->count >= XOB_SCREEN_MAX_RECTS) {
        return;
    }

    frame->rects[frame->count++] = (xob_screen_rect_t){
        .x = x1,
        .y = y1,
        .w = (int16_t)(x2 - x1),
        .h = (int16_t)(y2 - y1),
        .color = color,
    };
}

static void add_eye(xob_screen_frame_t *frame, const xob_eyes_frame_t *eyes, int16_t x) {
    int16_t open_h = (int16_t)((eyes->height * (int32_t)eyes->openness + 127) / 255);
    if (open_h < 2) {
        open_h = 2;
    }

    int16_t eye_y = (int16_t)(eyes->y + (eyes->height - open_h) / 2);
    add_rect(frame, x, eye_y, eyes->width, open_h, XOB_RGB565_WHITE);

    if (open_h < 10) {
        return;
    }

    int16_t pupil_w = (int16_t)(eyes->width / 4);
    int16_t pupil_h = (int16_t)(open_h / 3);
    if (pupil_w < 6) {
        pupil_w = 6;
    }
    if (pupil_h < 4) {
        pupil_h = 4;
    }

    int16_t pupil_x = (int16_t)(x + eyes->width / 2 + eyes->pupil_dx - pupil_w / 2);
    int16_t pupil_y = (int16_t)(eye_y + open_h / 2 + eyes->pupil_dy - pupil_h / 2);
    pupil_x = clamp_i16(pupil_x, x, (int16_t)(x + eyes->width - pupil_w));
    pupil_y = clamp_i16(pupil_y, eye_y, (int16_t)(eye_y + open_h - pupil_h));
    add_rect(frame, pupil_x, pupil_y, pupil_w, pupil_h, XOB_RGB565_BLACK);
}

xob_screen_frame_t xob_screen_render_eyes(const xob_eyes_frame_t *eyes) {
    xob_screen_frame_t frame = {0};
    if (eyes == 0) {
        return frame;
    }

    add_rect(&frame, 0, 0, XOB_SCREEN_WIDTH, XOB_SCREEN_HEIGHT, XOB_RGB565_BLACK);
    add_eye(&frame, eyes, eyes->left_x);
    add_eye(&frame, eyes, eyes->right_x);
    return frame;
}
