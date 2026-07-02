#include "eyes.h"

static uint8_t blink_openness(uint32_t tick_ms) {
    uint32_t phase = tick_ms % 3600;
    if (phase < 80) {
        return 255 - (uint8_t)(phase * 255 / 80);
    }
    if (phase < 160) {
        return (uint8_t)((phase - 80) * 255 / 80);
    }
    return 255;
}

xob_eyes_frame_t xob_eyes_frame(xob_eye_state_t state, uint32_t tick_ms) {
    xob_eyes_frame_t frame = {
        .left_x = 62,
        .right_x = 146,
        .y = 92,
        .width = 54,
        .height = 42,
        .pupil_dx = 0,
        .pupil_dy = 0,
        .openness = blink_openness(tick_ms),
    };

    switch (state) {
        case XOB_EYES_LISTENING:
            frame.width = 58;
            frame.height = 46;
            frame.openness = 255;
            break;
        case XOB_EYES_THINKING:
            frame.pupil_dx = ((tick_ms / 500) % 2 == 0) ? -6 : 6;
            break;
        case XOB_EYES_SPEAKING:
            frame.height = 40 + (int16_t)((tick_ms / 120) % 6);
            frame.openness = 255;
            break;
        case XOB_EYES_ERROR:
            frame.height = 24;
            frame.pupil_dy = 4;
            frame.openness = 180;
            break;
        case XOB_EYES_IDLE:
        default:
            break;
    }
    return frame;
}
