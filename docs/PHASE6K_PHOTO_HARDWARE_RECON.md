# Phase 6K Photo Hardware Recon

Phase 6K records hardware facts visible from user-provided board and battery photos. It does not flash the board.

## Photo Evidence

The photos are not committed because they came from a local chat/cache path. Only sanitized observations are recorded.

Visible board markings:

| Area | Marking | Conclusion |
|---|---|---|
| Main MCU | `ESP32-C3` | Confirms the board is ESP32-C3, matching `esptool`. |
| SPI flash | `ZB25VQ64...` | 64 Mbit SPI NOR flash family, matching the 8 MB flash detected by `esptool`. |
| Audio power amp | `8002D1` | 8002-family mono audio power amplifier for speaker output. |
| Crystal | `40.000 MHz` | Matches ESP32-C3 40 MHz crystal reported by `esptool`. |
| Board silk | `MIC` | Confirms a microphone area/connector, but not the mic interface pin map. |
| Board silk | `GND VCC DATA` | Confirms a 3-wire peripheral header/connector area, exact function not confirmed. |
| Board silk | `GND RX TX 4G` | Confirms a UART/4G-labeled header area, exact use not confirmed. |
| LCD FPC | `GMT154-03` | Confirms a 1.54-inch-class LCD flex/module marking. Public GoldenMorning GMT154-family references align with 1.54-inch 240x240 ST7789/SPI, but not this exact `-03` pinout. |
| Back-side PCB silk | `XHT-VB68Ai-4G` | Likely PCB model or internal board identifier; exact public schematic/source match not found. |
| Back-side PCB date | `2025-04-28` | Likely PCB revision or manufacturing date marker. |

Visible battery marking:

```text
103040 3.7V 1200mAh
20250503
```

This is a single-cell LiPo pouch battery. The `103040` marking is consistent with a 10 x 30 x 40 mm class cell.

Visible full-board layout:

- Left edge has three physical side buttons, matching the three boot-log button GPIOs.
- Top edge has separate small-wire connectors for battery/power, mic, and speaker/peripheral wiring.
- Right edge has USB-C.
- Bottom-right has the LCD FPC connector.
- LCD traces fan out near the FPC connector, but the photo does not expose pin labels or a complete trace map.
- The LCD flex marking reads `GMT154-03`; it is a module/flex identifier, not a pin label.
- The back side has PCB marking `XHT-VB68Ai-4G` plus date `2025-04-28`.

Public reference clues, not exact-match pin proof:

- GoldenMorning `GMT154-01/GMT154-02`: 1.54-inch 240x240 ST7789T3, SPI, 15-pin.
- GoldenMorning `GMT154-01M`: 1.54-inch 240x240 ST7789 module.
- GoldenMorning `GMT154-8P`: 1.54-inch 240x240 ST7789V, 4-line SPI, 8-pin.
- `docs/PHASE6L_EXTERNAL_SOURCE_REVIEW.md` rejects a third-party 12-pin table as direct pin proof for `GMT154-03`.

## What This Changes

- The 8 MB flash size is now confirmed by both software and photo evidence.
- The audio output path likely uses a simple external speaker amplifier after the ESP32-C3 audio path.
- Battery capacity can be documented as 1200 mAh.
- The board has a real LCD FPC connector, but still no safe LCD GPIO map.
- The three boot-log button GPIOs correspond to three visible side buttons.
- The LCD is likely a 1.54-inch-class ST7789/SPI TFT module, consistent with earlier public `zuowei-c3-realtime-lcd` references and GoldenMorning GMT154-family references.
- The board now has a searchable PCB identifier, but exact public matches for `XHT-VB68Ai-4G` were not found.

## What Remains Unknown

- LCD SPI pins: SCLK, MOSI, CS, DC, reset, and backlight.
- Exact pin assignment for `GMT154-03`.
- Microphone type and pins.
- Speaker amp enable/shutdown pin, if any.
- Battery divider ratio and charge/status pins.
- Exact role of the `GND VCC DATA` header.
- Exact role of the `GND RX TX 4G` header.
- Vendor meaning of `XHT-VB68Ai-4G`.

## Decision

Do not change firmware pin assignments from photo evidence alone.

The photos strengthen the hardware record, but they still do not provide a safe LCD pin map. Keep the LCD probe gate from `docs/PHASE6I_LCD_PROBE_GATE.md`.
