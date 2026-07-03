# Phase 6I LCD Pin Map And Probe Gate

Phase 6I checks whether the Zuowei LCD pin map can be obtained without guessing. It does not flash the board.

## Search Result

No public LCD pin map was found.

Checked:

- `78/xiaozhi-esp32` current `main` at `009701e`: no `zuowei` board definition under `main/boards`.
- `78/xiaozhi-esp32` tags `v1.5.9`, `v1.6.0`, and `v1.6.2`: no `zuowei-c3-realtime-lcd`, `zuowei-c3-lcd`, `zuowei`, or `D019` source hit before the scan was stopped.
- GitHub code search for exact board strings: no public source hit outside this repository.
- Public issue `#1285`: confirms ESP32-C3 plus 1.54 TFT LCD, but no pin map.
- Public discussion `#1483`: maintainer says a similar Zuowei C3 1.54 LCD board is likely not open-source.
- Later user-provided LCD flex photo shows `GMT154-03`. Public GoldenMorning GMT154-family pages align with 1.54-inch 240x240 ST7789/SPI modules, but no exact `GMT154-03` board pin map was found.
- Later user-provided back-side board photo shows `XHT-VB68Ai-4G` and date `2025-04-28`. Exact public searches for this marking did not find a schematic or source match.
- External source review in `docs/PHASE6L_EXTERNAL_SOURCE_REVIEW.md` confirms nearby GoldenMorning GMT154 variants, but also rejects a third-party 12-pin table as direct proof for this board.
- Stock binary recon in `docs/PHASE6M_STOCK_BINARY_LCD_PIN_RECON.md` infers the LCD GPIO map from the local stock firmware.

## Known Pins

Do not use these as LCD candidates unless a schematic proves otherwise:

| Pin | Evidence |
|---|---|
| GPIO9 | Button input |
| GPIO8 | Button input |
| GPIO7 | Button input |
| GPIO4 | Battery ADC, from `ADC_CHANNEL_4` |
| GPIO1 | LCD MOSI/SDA, inferred from stock binary |
| GPIO3 | LCD SCLK/SCL, inferred from stock binary |
| GPIO12 | LCD CS, inferred from stock binary |
| GPIO0 | LCD DC/RS, inferred from stock binary |
| GPIO2 | LCD reset, inferred from stock binary |
| GPIO5 | LCD backlight PWM, inferred from stock binary |
| GPIO21 | Probable power/charge/status input |

GPIO0 and GPIO2 were previously only known as outputs in the stock boot log. They now map to LCD DC and LCD reset with high confidence from stock binary inspection.

## Decision

ST7789 panel init can be implemented behind the inferred board profile, but it remains untested on hardware.

The next LCD validation step needs one of:

1. Physical trace/continuity check from the board.
2. Explicit approval to flash a temporary LCD probe or the reviewed custom firmware.
3. Manufacturer schematic/source, if it becomes available later.

## Probe Rules

If a probe is approved later, keep it narrow:

- Run `python3 scripts/check_flash_backup.py` first.
- Use the reviewed non-erase flash path from `docs/PHASE6G_FIRST_FLASH_REVIEW.md`.
- Test one candidate pin set at a time.
- Draw only a static color-bar or eye test pattern.
- Keep serial logs as the primary result channel.
- Do not erase NVS, `model`, or the backup.
- Do not commit WiFi, MAC, IP, token, or raw boot logs.

## Current Next Action

Ask Zuowei/vendor for the board schematic/source using the observed identifiers (`zuowei-c3-realtime-lcd`, `XHT-VB68Ai-4G`, `GMT154-03`), perform continuity testing, or provide explicit approval for a temporary LCD probe.
