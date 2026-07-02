# Phase 6A Firmware Skeleton

Phase 6A adds a minimal ESP-IDF project for the ESP32-C3 board. It does not flash the board.

## Scope

- ESP-IDF project directory: `firmware/esp32c3`
- ESP32-C3 target defaults
- 8 MB stock-compatible flash partition table
- NVS namespace: `xob`
- NVS keys:
  - `bridge_url`
  - `device_token`
- boot-time status logs

## Not Included Yet

- WiFi connection
- ST7789 drawing
- animated eyes
- HTTP `/device/hello`
- audio

Those need either confirmed pins or the next firmware slice.

## Build

Requires ESP-IDF installed locally:

```bash
cd firmware/esp32c3
idf.py set-target esp32c3
idf.py build
```

Do not run `idf.py flash` until the restore path is reviewed again.

The partition table mirrors the observed stock Xiaozhi layout. Do not replace it with a generic ESP-IDF template for this board.

## Local Check

This check does not require ESP-IDF:

```bash
python3 scripts/check_firmware_skeleton.py
```

Expected output:

```text
check_firmware_skeleton ok
```
