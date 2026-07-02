# Phase 6E Build Validation

Phase 6E adds the no-flash firmware build path.

## Local Status

This machine does not currently have ESP-IDF, PlatformIO, or Docker available in the shell.

The repository now includes `scripts/build_firmware.sh`. It uses an existing `idf.py`, `$IDF_PATH/export.sh`, or `~/esp/esp-idf/export.sh`, then runs:

```bash
idf.py set-target esp32c3
idf.py build
```

It does not run `flash`, `monitor`, or any serial-port command.

## CI Status

GitHub firmware CI is deferred because the current GitHub OAuth credential cannot create workflow files without the `workflow` scope.

## Check

Local repository checks:

```bash
bash -n scripts/build_firmware.sh
python3 scripts/check_firmware_skeleton.py
```

Expected output:

```text
check_firmware_skeleton ok
```

Do not flash until the restore path and target board are reviewed again.
