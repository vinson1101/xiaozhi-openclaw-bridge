#include "eyes.h"

static uint8_t blink_openness(uint32_t tick_ms) {
    uint32_t phase = tick_ms % 5600;
    if (phase < 5200) {
        return 255;
    }
    if (phase < 5280) {
        return 255 - (uint8_t)((phase - 5200) * 255 / 80);
    }
    if (phase < 5400) {
        return 0;
    }
    if (phase < 5480) {
        return (uint8_t)((phase - 5400) * 255 / 80);
    }
    return 255;
}

static int16_t idle_gaze_dx(uint32_t tick_ms) {
    static const int8_t gaze[] = {0, 5, 5, -3, -7, 2, 0};
    return gaze[(tick_ms / 4200) % (sizeof(gaze) / sizeof(gaze[0]))];
}

static int16_t idle_gaze_dy(uint32_t tick_ms) {
    static const int8_t gaze[] = {0, -2, 3, 1, -3, 2, 0};
    return gaze[(tick_ms / 5600) % (sizeof(gaze) / sizeof(gaze[0]))];
}

xob_eyes_frame_t xob_eyes_frame(xob_eye_state_t state, uint32_t tick_ms) {
    const int16_t eye_width = 62;
    const int16_t eye_height = 58;
    const int16_t eye_gap = 30;
    const int16_t face_width = (int16_t)(eye_width * 2 + eye_gap);
    const int16_t left_x = (int16_t)((240 - face_width) / 2);

    xob_eyes_frame_t frame = {
        .left_x = left_x,
        .right_x = (int16_t)(left_x + eye_width + eye_gap),
        .y = 88,
        .width = eye_width,
        .height = eye_height,
        .pupil_dx = idle_gaze_dx(tick_ms),
        .pupil_dy = idle_gaze_dy(tick_ms),
        .openness = blink_openness(tick_ms),
        .mouth_open = 0,
    };

    switch (state) {
        case XOB_EYES_LISTENING:
            frame.width = 66;
            frame.height = 62;
            frame.left_x = 39;
            frame.right_x = 135;
            frame.openness = 255;
            break;
        case XOB_EYES_THINKING:
            frame.pupil_dx = ((tick_ms / 500) % 2 == 0) ? -6 : 6;
            break;
        case XOB_EYES_SPEAKING:
            frame.openness = 255;
            frame.mouth_open = ((tick_ms / 360) % 2 == 0) ? 60 : 160;
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
