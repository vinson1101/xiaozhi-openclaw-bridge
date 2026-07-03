# Phase 6M Stock Binary LCD Pin Recon

Phase 6M statically inspects the local stock flash backup to infer the LCD pin map. It does not read from the board and does not write flash.

## Source

- Local ignored flash backup: `outputs/flash-backups/..._flash.bin`
- Parsed stock partition table:
  - `ota_0`: offset `0x100000`, size `0x380000`
  - `ota_1`: offset `0x480000`, size `0x380000`
- `ota_0` contains the active stock firmware strings for `zuowei-c3-realtime-lcd`.

Relevant stock strings found in `ota_0`:

- `./firmware/zuowei/zuowei-c3-realtime-lcd/zuowei-c3-realtime-lcd.cc`
- `void CustomBoard::InitializeLcdDisplay()`
- `esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io)`
- `esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel)`
- `spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO)`
- `PwmBacklight::PwmBacklight(gpio_num_t, bool)`

## Inferred LCD Pin Map

The following values were recovered by matching the stock binary's immediate values against ESP-IDF v5.3 LCD/SPI structure layouts.

| Signal | GPIO | Evidence | Confidence |
|---|---:|---|---|
| LCD MOSI / SDA | 1 | `spi_bus_config_t.mosi_io_num = 1` before `spi_bus_initialize(SPI2_HOST, ...)` | High |
| LCD SCLK / SCL | 3 | `spi_bus_config_t.sclk_io_num = 3` before `spi_bus_initialize(SPI2_HOST, ...)` | High |
| LCD MISO | -1 | `spi_bus_config_t.miso_io_num = -1`; display is write-only SPI | High |
| LCD CS | 12 | `esp_lcd_panel_io_spi_config_t.cs_gpio_num = 12` | High |
| LCD DC / RS | 0 | `esp_lcd_panel_io_spi_config_t.dc_gpio_num = 0` | High |
| LCD RESET | 2 | `esp_lcd_panel_dev_config_t.reset_gpio_num = 2` | High |
| LCD backlight PWM | 5 | Single `PwmBacklight` constructor call passes `pin = 5`, `output_invert = 0` | High |

Other recovered LCD settings:

- SPI host: `SPI2_HOST`
- SPI mode: `3`
- Pixel clock: `80,000,000 Hz`
- Transaction queue depth: `10`
- LCD command bits: `8`
- LCD parameter bits: `8`
- Bits per pixel: `16`
- Max transfer size: `240 * 240 * 2 = 115200`

## Important Correction

Do not use GPIO20 as the backlight pin.

During manual inspection, a separate board-level constructor call used values `20` and `10`, but the unique `PwmBacklight` constructor call resolves to GPIO5. GPIO20 remains unrelated to the confirmed LCD path until separately identified.

## Status

This is stronger than photo-only evidence because it comes from the stock firmware for this exact board family.

Still, it is not a live-tested custom firmware result. Treat the map as high-confidence inferred until one of these confirms it:

1. Continuity testing from LCD FPC/backlight circuitry to ESP32-C3 pins.
2. A narrow LCD probe firmware approved by the user.
3. First custom firmware boot with the reviewed restore path available.

## Decision

The firmware can now add an ST7789 implementation behind this board profile, but flashing still requires explicit approval and the restore checklist from `docs/PHASE6G_FIRST_FLASH_REVIEW.md`.
