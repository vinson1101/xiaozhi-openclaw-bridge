# Phase 5A Hardware Recon

Phase 5A uses the existing local flash backup. It does not read from the board and does not write flash.

## Confirmed

- Full local flash backup exists under ignored `outputs/flash-backups/`.
- Flash backup size is 8 MB.
- Partition-table backup exists and is 4096 bytes.
- Stock board target string: `zuowei-c3-realtime-lcd`.
- Display driver string: `ST7789`.
- Display path includes ESP-IDF SPI LCD calls such as `esp_lcd_new_panel_io_spi`, `SPI2_HOST`, and `esp_lcd_new_panel_st7789`.
- Firmware contains modules or symbols for `AudioCodec`, `I2S1`, `Backlight`, `Battery`, buttons, and ADC.
- ADC button/battery clues include `ADC_CHANNEL_4`.
- OTA flow exists in stock firmware, including `ota_0`, `ota_1`, and `otadata`.
- Sanitized boot log confirms stock version `1.6.1`, SKU `zuowei-c3-realtime-lcd`, running partition `ota_0`, and display/backlight init.
- Boot log confirms three button GPIOs: 9, 8, and 7.
- Battery ADC is `ADC_CHANNEL_4`, which maps to GPIO4 on ESP32-C3.

## Not Confirmed

- Exact LCD pin map: SCLK, MOSI, CS, DC, reset, and backlight GPIO.
- Exact button semantics for GPIO9, GPIO8, and GPIO7.
- Exact microphone/speaker wiring and codec model.
- Battery ADC divider and voltage calibration.
- Exact role of GPIO21, GPIO0, and GPIO2.
- Whether the current board exposes enough free GPIO for additional controls.

## Board Decision

Use the current ESP32-C3 board for Phase 6 display/control bring-up.

Do not assume it is the final voice board. ESP32-C3 is enough for WiFi, bridge connection, ST7789 eyes, simple UI states, and server-routed text commands. For the full voice loop, keep ASR/TTS on the bridge server. If local wake word, heavier audio buffering, or richer animation becomes required, move to ESP32-S3.

## Firmware Direction

Start with a minimal ESP-IDF firmware skeleton:

- WiFi
- NVS bridge URL/token
- HTTP JSON `hello`
- ST7789 static eyes
- blink animation
- bridge connection state

Skip audio in the first firmware build.

## Check

```bash
python3 scripts/check_flash_backup.py
```

Expected output:

```text
check_flash_backup ok
```
