# Phase 6N LCD Driver

Phase 6N wires the inferred LCD pin map into the ESP32-C3 firmware. This phase originally stopped at local build validation; later hardware flashing and visual validation are recorded in `docs/PHASE6O_FIRST_CUSTOM_FLASH.md`.

## Implementation

Added:

- `firmware/esp32c3/main/lcd.c`
- `firmware/esp32c3/main/lcd.h`

The driver:

- initializes SPI2 for ST7789,
- initializes the ST7789 panel through `esp_lcd`,
- enables PWM backlight,
- draws the existing eye-render rectangles to the display.
- draws the boot eye frame before WiFi/Bridge provisioning, so an unprovisioned device still exercises the LCD.

## Board Profile

The implementation uses the stock-binary inferred pins from `docs/PHASE6M_STOCK_BINARY_LCD_PIN_RECON.md`:

| Signal | GPIO |
|---|---:|
| MOSI / SDA | 1 |
| SCLK / SCL | 3 |
| CS | 12 |
| DC / RS | 0 |
| RESET | 2 |
| Backlight PWM | 5 |

LCD settings:

- SPI host: `SPI2_HOST`
- SPI mode: `3`
- Pixel clock: `80 MHz`
- Command bits: `8`
- Parameter bits: `8`
- Color depth: `RGB565`
- Resolution: `240x240`

## Build Check

```bash
./scripts/build_firmware.sh
python3 scripts/check_firmware_skeleton.py
python3 scripts/check_eye_render.py
python3 scripts/check_flash_backup.py
```

Build result after adding the LCD driver:

```text
xob_esp32c3.bin binary size 0xed8d0 bytes
Smallest app partition is 0x380000 bytes
0x292730 bytes (73%) free
```

## Safety Status

The reviewed flash path remains `docs/PHASE6G_FIRST_FLASH_REVIEW.md`. Later non-erase hardware validation is recorded in `docs/PHASE6O_FIRST_CUSTOM_FLASH.md`.
