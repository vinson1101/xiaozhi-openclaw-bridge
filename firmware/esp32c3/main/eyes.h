#pragma once

#include <stdint.h>

typedef enum {
    XOB_EYES_IDLE = 0,
    XOB_EYES_LISTENING,
    XOB_EYES_THINKING,
    XOB_EYES_SPEAKING,
    XOB_EYES_ERROR,
} xob_eye_state_t;

typedef struct {
    int16_t left_x;
    int16_t right_x;
    int16_t y;
    int16_t width;
    int16_t height;
    int16_t pupil_dx;
    int16_t pupil_dy;
    uint8_t openness;
} xob_eyes_frame_t;

xob_eyes_frame_t xob_eyes_frame(xob_eye_state_t state, uint32_t tick_ms);
