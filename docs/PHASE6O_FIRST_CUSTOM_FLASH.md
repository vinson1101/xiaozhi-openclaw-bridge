# Phase 6O First Custom Flash And Avatar Check

Phase 6O records the first successful custom firmware write to the ESP32-C3 board and the visual LCD/avatar validation.

## Result

The board booted custom firmware with the inferred ST7789 pin map:

| Signal | GPIO |
|---|---:|
| MOSI / SDA | 1 |
| SCLK / SCL | 3 |
| CS | 12 |
| DC / RS | 0 |
| RESET | 2 |
| Backlight PWM | 5 |

The LCD orientation that matched the physical screen:

- `swap_xy=true`
- `mirror_x=false`
- `mirror_y=true`
- `gap=80,0`

The visible avatar uses a black background, white rounded eyes, black pupils, white highlights, and a M5Stack-inspired horizontal mouth line. The redraw loop skips identical frames to reduce flicker.

## Validation

Validated locally:

```bash
python3 scripts/check_eye_render.py
python3 scripts/check_firmware_skeleton.py
python3 scripts/check_flash_backup.py
./scripts/build_firmware.sh
```

Build result after avatar tuning:

```text
xob_esp32c3.bin binary size 0xee1e0 bytes
Smallest app partition is 0x380000 bytes
0x291e20 bytes (73%) free
```

The visual check was done through the local Mac camera preview. No device MAC, token, raw boot log, flash backup, or private configuration is recorded here.

## Safety

The flash path used the reviewed non-erase write command with `@flash_args`. `erase_flash` was not used.
