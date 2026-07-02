# Phase 6H Boot And Pin Recon

Phase 6H captures sanitized boot evidence and pin-map findings. It does not flash the board.

## Sources

- Local stock flash backup strings.
- Live serial boot log from `/dev/cu.usbmodem3101`, sanitized before documentation.
- `78/xiaozhi-esp32` current `main` at `009701e`; no `zuowei` board definition was present under `main/boards`.
- Public GitHub issue: `zuowei-c3-realtime-lcd` is reported as ESP32-C3 with a 1.54 TFT LCD: <https://github.com/78/xiaozhi-esp32/issues/1285>.
- Public GitHub discussion: a similar Zuowei C3 1.54 LCD board was described by the maintainer as likely not open-source: <https://github.com/78/xiaozhi-esp32/discussions/1483>.

Do not commit raw serial logs. They can include MAC addresses, local WiFi names, IP addresses, and cloud connection details.

## Sanitized Boot Facts

- Stock project: `xiaozhi`
- Stock version: `1.6.1`
- Stock compile time: `Apr 24 2025 09:29:32`
- Stock ESP-IDF: `v5.3.2-873-gd5b8419620-dirty`
- Board SKU: `zuowei-c3-realtime-lcd`
- Running stock partition: `ota_0`
- Display path reaches `LcdDisplay: Adding LCD screen`.
- Backlight path reaches `Backlight: Set brightness to 100`.
- Audio path toggles `VbAduioCodec` input and output enable.

## Pin Evidence

| Area | Evidence | Status |
|---|---|---|
| Button GPIO 9 | Boot log configures `GPIO[9]` as input with pull-up through IoT Button. | Confirmed |
| Button GPIO 8 | Boot log configures `GPIO[8]` as input with pull-up through IoT Button. | Confirmed |
| Button GPIO 7 | Boot log configures `GPIO[7]` as input with pull-up through IoT Button. | Confirmed |
| Battery ADC | Stock strings use `ADC_CHANNEL_4`; ESP-IDF maps ESP32-C3 ADC1 channel 4 to GPIO4. | Confirmed |
| GPIO 21 | Boot log configures `GPIO[21]` as input with no pull near power setup. | Probable power/charge/status input |
| GPIO 0 and GPIO 2 | Boot log configures both as outputs before display init finishes. | Unknown display/power role |
| LCD controller | Stock strings confirm `SPI2_HOST` and `esp_lcd_new_panel_st7789`. | Confirmed controller path |
| LCD SPI pins | SCLK, MOSI, CS, DC, reset, and backlight GPIO are not exposed in logs or public source. | Not confirmed |
| Audio | Stock strings confirm audio codec classes and `I2S1`; boot log confirms codec enable calls. | Pin map not confirmed |

## Decision

Do not hardcode LCD pins yet.

The three button pins and battery ADC pin can be used in future firmware behind board-specific code. LCD bring-up still needs one of:

- manufacturer schematic or board source,
- physical trace/continuity check,
- or an explicitly approved probe firmware that only tests reviewed candidate pins.

Until then, first custom firmware should keep serial/WiFi status as the primary bring-up path and treat the screen as optional.
