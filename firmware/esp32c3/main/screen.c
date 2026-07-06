#include "screen.h"

#define XOB_RGB565_BLACK 0x0000
#define XOB_RGB565_WHITE 0xffff
#define XOB_RGB565_DIM 0x4208
#define XOB_RGB565_AMBER 0xfd20
#define XOB_RGB565_GREEN 0x07e0
#define XOB_RGB565_RED 0xf800

#define XOB_EYE_BAND_HEIGHT 2

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

static int16_t ellipse_half_width(int16_t rx, int16_t ry, int16_t dy) {
    int32_t rx2 = (int32_t)rx * rx;
    int32_t ry2 = (int32_t)ry * ry;
    int32_t limit = rx2 * ry2;
    int32_t dy2_rx2 = (int32_t)dy * dy * rx2;

    for (int16_t x = rx; x >= 0; --x) {
        int32_t x2_ry2 = (int32_t)x * x * ry2;
        if (x2_ry2 + dy2_rx2 <= limit) {
            return x;
        }
    }
    return 0;
}

static void add_ellipse(xob_screen_frame_t *frame, int16_t cx, int16_t cy, int16_t rx, int16_t ry, uint16_t color) {
    if (rx <= 0 || ry <= 0) {
        return;
    }

    for (int16_t dy = (int16_t)-ry; dy <= ry; dy = (int16_t)(dy + XOB_EYE_BAND_HEIGHT)) {
        int16_t band_h = XOB_EYE_BAND_HEIGHT;
        if (dy + band_h > ry + 1) {
            band_h = (int16_t)(ry + 1 - dy);
        }
        int16_t half_w = ellipse_half_width(rx, ry, dy);
        add_rect(frame, (int16_t)(cx - half_w), (int16_t)(cy + dy), (int16_t)(half_w * 2 + 1), band_h, color);
    }
}

static void add_eye(xob_screen_frame_t *frame, const xob_eyes_frame_t *eyes, int16_t x) {
    int16_t open_h = (int16_t)((eyes->height * (int32_t)eyes->openness + 127) / 255);
    if (open_h < 2) {
        open_h = 2;
    }

    int16_t cx = (int16_t)(x + eyes->width / 2);
    int16_t cy = (int16_t)(eyes->y + eyes->height / 2);
    if (open_h < 8) {
        add_rect(frame, (int16_t)(x + 5), (int16_t)(cy - 1), (int16_t)(eyes->width - 10), 3, XOB_RGB565_WHITE);
        return;
    }

    int16_t outer_rx = (int16_t)(eyes->width / 2);
    int16_t outer_ry = (int16_t)(open_h / 2);
    int16_t inner_rx = (int16_t)(outer_rx / 2);
    int16_t inner_ry = (int16_t)(outer_ry / 2);
    if (inner_rx < 7) {
        inner_rx = 7;
    }
    if (inner_ry < 5) {
        inner_ry = 5;
    }

    int16_t gaze_cx = clamp_i16((int16_t)(cx + eyes->pupil_dx), (int16_t)(x + 12), (int16_t)(x + eyes->width - 12));
    int16_t gaze_cy = clamp_i16((int16_t)(cy + eyes->pupil_dy), (int16_t)(cy - outer_ry + 10), (int16_t)(cy + outer_ry - 10));

    add_ellipse(frame, cx, cy, outer_rx, outer_ry, XOB_RGB565_WHITE);
    add_ellipse(frame, gaze_cx, gaze_cy, inner_rx, inner_ry, XOB_RGB565_BLACK);
    add_ellipse(frame, (int16_t)(gaze_cx - inner_rx / 3), (int16_t)(gaze_cy - inner_ry / 3), 5, 4, XOB_RGB565_WHITE);
}

static void add_mouth(xob_screen_frame_t *frame, const xob_eyes_frame_t *eyes) {
    int16_t w = (int16_t)(48 + (eyes->mouth_open * 12) / 255);
    int16_t h = (int16_t)(3 + (eyes->mouth_open * 7) / 255);
    int16_t x = (int16_t)((XOB_SCREEN_WIDTH - w) / 2);
    int16_t y = (int16_t)(eyes->y + eyes->height + 28 - h / 2);
    add_rect(frame, x, y, w, h, XOB_RGB565_WHITE);
}

static uint16_t status_color(xob_screen_status_t status) {
    switch (status) {
        case XOB_SCREEN_STATUS_PENDING:
            return XOB_RGB565_AMBER;
        case XOB_SCREEN_STATUS_OK:
            return XOB_RGB565_GREEN;
        case XOB_SCREEN_STATUS_ERROR:
            return XOB_RGB565_RED;
        case XOB_SCREEN_STATUS_OFF:
        default:
            return XOB_RGB565_DIM;
    }
}

static void add_wifi_status(xob_screen_frame_t *frame, int16_t x, int16_t y, xob_screen_status_t status) {
    uint16_t color = status_color(status);
    add_rect(frame, x, (int16_t)(y + 8), 4, 4, color);
    add_rect(frame, (int16_t)(x + 7), (int16_t)(y + 4), 4, 8, color);
    add_rect(frame, (int16_t)(x + 14), y, 4, 12, color);
}

static void add_bridge_status(xob_screen_frame_t *frame, int16_t x, int16_t y, xob_screen_status_t status) {
    uint16_t color = status_color(status);
    add_rect(frame, x, (int16_t)(y + 2), 10, 4, color);
    add_rect(frame, (int16_t)(x + 14), (int16_t)(y + 2), 10, 4, color);
    add_rect(frame, (int16_t)(x + 8), (int16_t)(y + 5), 8, 3, color);
}

static void add_status_row(xob_screen_frame_t *frame, xob_screen_status_t wifi_status, xob_screen_status_t bridge_status) {
    if (wifi_status != XOB_SCREEN_STATUS_OFF) {
        add_wifi_status(frame, 8, 8, wifi_status);
    }
    if (bridge_status != XOB_SCREEN_STATUS_OFF) {
        add_bridge_status(frame, 34, 10, bridge_status);
    }
}

xob_screen_frame_t xob_screen_render_avatar(
    const xob_eyes_frame_t *eyes,
    xob_screen_status_t wifi_status,
    xob_screen_status_t bridge_status
) {
    xob_screen_frame_t frame = {0};
    if (eyes == 0) {
        return frame;
    }

    add_rect(&frame, 0, 0, XOB_SCREEN_WIDTH, XOB_SCREEN_HEIGHT, XOB_RGB565_BLACK);
    add_eye(&frame, eyes, eyes->left_x);
    add_eye(&frame, eyes, eyes->right_x);
    add_mouth(&frame, eyes);
    add_status_row(&frame, wifi_status, bridge_status);
    return frame;
}

xob_screen_frame_t xob_screen_render_eyes(const xob_eyes_frame_t *eyes) {
    return xob_screen_render_avatar(eyes, XOB_SCREEN_STATUS_OFF, XOB_SCREEN_STATUS_OFF);
}
