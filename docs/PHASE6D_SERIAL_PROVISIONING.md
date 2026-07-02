# Phase 6D USB Serial Provisioning

Phase 6D adds the first config write path for custom firmware.

## Scope

- If `xob` config is missing, firmware enters USB serial provisioning.
- Serial prompts collect:
  - `bridge_url`
  - `device_token`
  - `wifi_ssid`
  - `wifi_password`
- Values are written only to NVS namespace `xob`.
- `bridge_url` and `wifi_ssid` are required.
- `device_token` and `wifi_password` may be empty.
- Firmware reboots after a successful write.

The firmware does not print the entered values.

## Serial Flow

Open the ESP32-C3 serial console and enter values when prompted:

```text
XOB provisioning over USB serial
Values are stored in NVS namespace 'xob'. device_token and wifi_password may be empty.
bridge_url: http://<bridge-host>:<port>
device_token:
wifi_ssid: <ssid>
wifi_password:
```

Use real values only on the local serial console. Do not commit them.

## NVS Safety

The firmware refuses automatic `nvs_flash_erase()` recovery.

If NVS init reports no free pages or a new version, the firmware stops instead of erasing the stock NVS partition. Recovery requires an explicit restore or erase decision.

## Check

This repository check does not need ESP-IDF:

```bash
python3 scripts/check_firmware_skeleton.py
```

Expected output:

```text
check_firmware_skeleton ok
```

Do not flash until the restore path and target board are reviewed again.
