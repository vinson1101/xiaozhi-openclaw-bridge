# Phase 6B WiFi And Device Hello

Phase 6B extends the ESP32-C3 firmware skeleton with WiFi STA mode and one HTTP JSON hello.

## Scope

- NVS config keys:
  - `bridge_url`
  - `device_token`
  - `wifi_ssid`
  - `wifi_password`
- WiFi STA connection
- HTTP `POST <bridge_url>/device/hello`
- `Authorization: Bearer <device_token>` header when a token is configured
- device id derived from the WiFi MAC suffix

The firmware logs whether values are configured, not the actual URL, SSID, or token.

## Not Included Yet

- provisioning UI and write path
- ST7789 screen drawing
- animated eyes
- command polling or WebSocket
- audio

## Configuration

This firmware does not reuse the original Xiaozhi cloud configuration.

It reads a separate `xob` NVS namespace. Missing keys put the firmware into provisioning mode.

The partition table now mirrors the stock Xiaozhi layout, so future flashing can preserve the original `nvs`, `phy_init`, `model`, and OTA slots unless an explicit full restore/erase is chosen.

## Check

This repository check does not need ESP-IDF:

```bash
python3 scripts/check_firmware_skeleton.py
```

Expected output:

```text
check_firmware_skeleton ok
```

Build still requires ESP-IDF:

```bash
cd firmware/esp32c3
idf.py set-target esp32c3
idf.py build
```

Do not flash until the restore path and target board are reviewed again.

## M5Stack Borrowing

This phase also adds the first M5Stack-inspired avatar state model under `firmware/esp32c3/main/eyes.*`.

It is original ESP-IDF C code. It does not vendor M5Stack Avatar, StackChan, M5GFX, M5Unified, or GPLv3 RoboEyes source.
