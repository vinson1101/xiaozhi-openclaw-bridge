# Phase 6J Open C3 Reference Check

Phase 6J checks open XiaoZhi C3 boards for reusable schematic clues. It does not flash the board.

## Checked Sources

- `78/xiaozhi-esp32` at commit `009701e71c959f0f9253c5e858e6a231fe206920`.
- Lichuang C3 docs: <https://openkits-wiki.easyeda.com/zh-hans/szpi-esp32c3/open-source-hardware/open-source-hardware.html>
- Lichuang C3 open hardware project: <https://oshwhub.com/li-chuang-kai-fa-ban/xd-esp32c3-aiot-v1_3_2>

## Relevant C3 Board Configs

| Board | Display | Display pins from open config | Conflict with Zuowei evidence |
|---|---|---|---|
| `lichuang-c3-dev` | ST7789, 320x240 | SCK 3, MOSI 5, DC 6, CS 4, BL 2 | CS uses GPIO4, but Zuowei uses GPIO4 as battery ADC |
| `surfer-c3-1.14tft` | ST7789, 240x135 | SCK 3, MOSI 5, DC 6, CS 4, BL 13 | CS uses GPIO4, but Zuowei uses GPIO4 as battery ADC |
| `magiclick-c3` | 128x128 LCD | SDA 12, SCL 13, CS 20, DC 21, BL 9 | BL uses GPIO9 button; DC uses probable GPIO21 status input |
| `magiclick-c3-v2` | 128x128 LCD | SDA 13, SCL 12, CS 20, DC 21, BL 9 | BL uses GPIO9 button; DC uses probable GPIO21 status input |
| `xmini-c3` | 128x64 OLED | No ST7789 SPI LCD pins | Not useful for LCD |
| `kevin-c3` | No LCD in checked config | No ST7789 SPI LCD pins | Not useful for LCD |

## Result

None of the open C3 board pin maps can be copied to the Zuowei board.

The closest ST7789 examples use GPIO4 as LCD CS. On this Zuowei board, stock firmware strings and boot recon identify `ADC_CHANNEL_4`, and ESP32-C3 maps that to GPIO4. Treating GPIO4 as LCD CS would collide with the battery ADC evidence.

The Magiclick C3 LCD maps also collide with known/probable Zuowei pins.

## Decision

Keep the Phase 6I gate:

- Do not hardcode LCD pins from another C3 board.
- Ask Zuowei for schematic/source, or inspect traces physically.
- If neither is available, use only an explicitly approved temporary probe firmware.

The open C3 references are useful for driver shape, not for Zuowei pin selection.
