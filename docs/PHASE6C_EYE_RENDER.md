# Phase 6C Eye Render Commands

Phase 6C turns the avatar eye geometry into screen-ready draw commands without flashing the board.

## Scope

- Adds `firmware/esp32c3/main/screen.*`.
- Converts `xob_eyes_frame_t` into clipped RGB565 rectangles for a 240x240 ST7789-style screen.
- Keeps the renderer independent from LCD GPIO, SPI bus setup, and `esp_lcd_panel_draw_bitmap`.
- Logs the generated rectangle count from `app_main`.

## Why This Stops Before ST7789 Init

The stock flash confirms an `ST7789` path, but the exact LCD pins are not confirmed yet.

Hardcoding guessed GPIOs would make the first custom flash riskier than useful. This phase creates the payload the ST7789 driver will consume once the pin map is confirmed.

## Draw Model

The render frame contains up to five rectangles:

- full black background
- left white eye
- left black pupil
- right white eye
- right black pupil

Blinking is represented by shrinking eye height from `openness`.

## Check

This repository check does not need ESP-IDF:

```bash
python3 scripts/check_firmware_skeleton.py
python3 scripts/check_eye_render.py
```

Expected output:

```text
check_firmware_skeleton ok
check_eye_render ok
```

Do not flash until the restore path and target board are reviewed again.
