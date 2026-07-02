# Phase 6G First Flash Review

Phase 6G reviews the first custom firmware write path. It does not flash the board.

## Current Safety State

- Full 8 MB stock flash backup exists under ignored `outputs/flash-backups/`.
- `python3 scripts/check_flash_backup.py` passes.
- ESP-IDF `v5.3.5` build passes.
- Custom firmware binary: `firmware/esp32c3/build/xob_esp32c3.bin`.
- Custom app size: `0xe22b0`, with 75% of the smallest app partition free.
- The stock partition layout and custom partition layout match.

## Reviewed First-Flash Candidate

Use ESP-IDF's generated flash arguments, but only after explicit approval:

```bash
source /Users/vinson/esp/esp-idf-v5.3.5/export.sh
cd firmware/esp32c3/build
python -m esptool --chip esp32c3 -p <PORT> -b 460800 \
  --before default_reset --after hard_reset write_flash @flash_args
```

`flash_args` currently writes:

```text
0x0      bootloader/bootloader.bin
0x8000   partition_table/partition-table.bin
0xd000   ota_data_initial.bin
0x100000 xob_esp32c3.bin
```

This is not a full-chip erase. It preserves the stock `nvs`, `phy_init`, `model`, and `ota_1` regions.

Do not use app-only flashing for the first custom write. The stock `otadata` is not blank, so writing only `ota_0` may not boot the new app.

## Pre-Flash Checklist

Run these before any write:

```bash
python3 scripts/check_flash_backup.py
./scripts/build_firmware.sh
ls /dev/cu.usbmodem*
```

Then review:

- The USB port is the intended ESP32-C3 board.
- The board still reports ESP32-C3 and 8 MB flash.
- The command uses `@flash_args`, not `erase_flash`.
- The user explicitly approves the write.

## Restore Command

If the custom firmware needs to be reverted, restore the full local backup:

```bash
cd /Users/vinson/Projects/github/vinson1101/xiaozhi-openclaw-bridge
source /Users/vinson/esp/esp-idf-v5.3.5/export.sh
python -m esptool --chip esp32c3 --port <PORT> --baud 460800 \
  write_flash 0x0 outputs/flash-backups/<backup-file>.bin
```

Before restoring, confirm the backup file is exactly 8 MB and belongs to this board.

## Do Not Commit

- Flash backup binaries
- Device MAC/serial
- WiFi config
- Device tokens
- VPS hostnames, IPs, usernames, or keys
