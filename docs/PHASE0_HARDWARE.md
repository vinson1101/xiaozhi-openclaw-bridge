# Phase 0 Hardware Baseline

Date: 2026-07-02

## Public Hardware Record

- Port observed on macOS: `/dev/cu.usbmodem3101`
- USB device: Espressif `USB JTAG/serial debug unit`
- USB VID:PID: `303A:1001`
- Chip: ESP32-C3 QFN32 rev v0.4
- Features: WiFi, BLE
- Crystal: 40 MHz
- USB mode: USB-Serial/JTAG
- Flash manufacturer/device: `5e:4017`
- Flash size: 8 MB
- Device MAC/serial: recorded locally, not published

## Firmware Clues

- Stock firmware project: `xiaozhi`
- Stock firmware version observed earlier: `1.6.1`
- Board/firmware type observed earlier: `zuowei-c3-realtime-lcd`
- Display driver string observed earlier: `ST7789`

## Backup Status

- Full flash backup: completed
- Backup size: 8 MB
- Backup location: ignored local `outputs/flash-backups/`
- Full backup filename and SHA256: recorded in ignored local manifest
- Backup binary: not committed and must not be uploaded to GitHub

## Partition Summary

The partition table was extracted from the local backup.

| Name | Type/SubType | Offset | Size |
|---|---:|---:|---:|
| `nvs` | data/nvs | `0x009000` | `0x004000` |
| `otadata` | data/ota | `0x00d000` | `0x002000` |
| `phy_init` | data/phy | `0x00f000` | `0x001000` |
| `model` | data/0x82 | `0x010000` | `0x0f0000` |
| `ota_0` | app/ota_0 | `0x100000` | `0x380000` |
| `ota_1` | app/ota_1 | `0x480000` | `0x380000` |

## Restore Command Template

Do not run this unless explicitly restoring the original board state.

```bash
python -m esptool --chip esp32c3 --port /dev/cu.usbmodem3101 --baud 460800 \
  write_flash 0x0 outputs/flash-backups/<backup-file>.bin
```

## Safety Gate

No firmware write should happen until:

- the local backup manifest is present,
- the backup file exists and is 8 MB,
- the restore command target is reviewed,
- and the user explicitly agrees to write flash.
