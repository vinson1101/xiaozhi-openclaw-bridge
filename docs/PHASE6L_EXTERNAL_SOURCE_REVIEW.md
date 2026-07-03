# Phase 6L External Source Review

Phase 6L records externally found hardware clues and separates verified facts from unverified pinout guesses. It does not flash the board.

## Sources Checked

- GoldenMorning `GMT154-01/GMT154-02` page: <https://goldenmorninglcd.com/tft-display/1.54-inch-240x240-st7789t3-gmt154-02/>
- GoldenMorning `GMT154-01M` page: <https://goldenmorninglcd.com/tft-display-module/1.54-inch-240x240-st7789-gmt154-01m/>
- GoldenMorning `1.54TFT-8P` page: <https://goldenmorninglcd.com/tft-display-module/1.54-inch-240x240-st7789v-1.54tft-8p/>
- GoldenMorning `1.54TFT-10P` page: <https://goldenmorninglcd.com/tft-display-module/1.54-inch-240x240-st7789v-1.54tft-10p/>
- Public `78/xiaozhi-esp32` issue `#1285`: <https://github.com/78/xiaozhi-esp32/issues/1285>
- Public `78/xiaozhi-esp32` discussion `#1483`: <https://github.com/78/xiaozhi-esp32/discussions/1483>

## Verified Or Strongly Supported

- `GMT154` is a GoldenMorning 1.54-inch, 240x240, ST7789-family display line.
- GoldenMorning publishes multiple 1.54-inch ST7789/SPI variants with different connector styles: 15P, 10P, 8P, and module boards.
- `GMT154-01/GMT154-02` is listed as ST7789T3, 240x240, SPI, 15P.
- `GMT154-01M` is a module-board variant with an 8-pin-style module pinout: GND, VCC, SCL, SDA, RST, DC, CS, BL.
- `1.54TFT-8P` is listed as ST7789V, 240x240, 4-line SPI, 8P.
- `1.54TFT-10P` is listed as ST7789V, 240x240, 4-line SPI, 10P.
- GitHub issue `#1285` independently reports `zuowei-c3-realtime-lcd` as ESP32-C3 with a 1.54-inch TFT LCD.
- GitHub discussion `#1483` reports a similar ESP32-C3, 8 MB flash, 1.54 LCD, 3-button board as non-open-source and mentions `ai@zuoweitech.cn` as a vendor firmware contact.

## Rejected As Direct Pin Proof

The user-provided external summary included a 12-pin `GMT154-01` style pin table. Do not use that table directly.

Reason: GoldenMorning's public pages for the nearby variants found in this review show 15P, 10P, and 8P/module pinouts. They do not prove the exact `GMT154-03` FPC pin assignment on this board.

## Current Interpretation

The safest interpretation is:

- LCD controller family: ST7789 or ST7789-compatible.
- Display geometry: 240x240.
- Electrical bus: SPI.
- Board GPIO map: still unknown.
- Exact `GMT154-03` FPC pinout: still unknown.
- Exact backlight wiring: still unknown.

## Decision

Do not wire firmware to a guessed LCD GPIO map from external summaries.

Use the external sources to constrain the driver choice and display geometry only. The next safe step remains one of:

1. Get schematic or pin map from Zuowei/vendor.
2. Use continuity testing to map LCD FPC pins to ESP32-C3 GPIOs.
3. Flash a narrow temporary LCD probe only after explicit approval.
