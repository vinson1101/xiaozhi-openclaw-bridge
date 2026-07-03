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
- User-provided board photo confirms ESP32-C3, ZB25VQ64-series 64 Mbit flash, 8002-family audio amplifier, 40 MHz crystal, and a `MIC` silk label.
- User-provided battery photo confirms a `103040` 3.7 V 1200 mAh LiPo pouch cell.
- User-provided full-board photo confirms three visible side buttons, a bottom-right LCD FPC connector, USB-C, and `GND RX TX 4G` header silk.
- User-provided LCD flex photo shows `GMT154-03`, likely aligned with the GoldenMorning GMT154-family 1.54-inch 240x240 ST7789/SPI LCD flex/module references, but not a pin label.
- User-provided back-side board photo shows PCB marking `XHT-VB68Ai-4G` and date `2025-04-28`.

## Not Confirmed

- Exact LCD pin map: SCLK, MOSI, CS, DC, reset, and backlight GPIO.
- Exact pin assignment for LCD flex marking `GMT154-03`.
- Exact button semantics for GPIO9, GPIO8, and GPIO7.
- Exact microphone/speaker wiring and codec model.
- Battery ADC divider and voltage calibration.
- Speaker amp enable/shutdown pin, if any.
- Exact role of GPIO21, GPIO0, and GPIO2.
- Exact role of the `GND RX TX 4G` header.
- Whether `XHT-VB68Ai-4G` maps to a public schematic, source tree, or vendor board name.
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
