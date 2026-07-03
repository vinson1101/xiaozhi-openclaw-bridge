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

Visible battery marking:

```text
103040 3.7V 1200mAh
20250503
```

This is a single-cell LiPo pouch battery. The `103040` marking is consistent with a 10 x 30 x 40 mm class cell.

## What This Changes

- The 8 MB flash size is now confirmed by both software and photo evidence.
- The audio output path likely uses a simple external speaker amplifier after the ESP32-C3 audio path.
- Battery capacity can be documented as 1200 mAh.

## What Remains Unknown

- LCD SPI pins: SCLK, MOSI, CS, DC, reset, and backlight.
- Microphone type and pins.
- Speaker amp enable/shutdown pin, if any.
- Battery divider ratio and charge/status pins.
- Exact role of the `GND VCC DATA` header.

## Decision

Do not change firmware pin assignments from photo evidence alone.

The photos strengthen the hardware record, but they still do not provide a safe LCD pin map. Keep the LCD probe gate from `docs/PHASE6I_LCD_PROBE_GATE.md`.
