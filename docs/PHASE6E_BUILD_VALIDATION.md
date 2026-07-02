# Phase 6E Build Validation

Phase 6E adds the no-flash firmware build path.

## Local Status

ESP-IDF `v5.3.5` is installed locally at:

```text
/Users/vinson/esp/esp-idf-v5.3.5
```

The repository now includes `scripts/build_firmware.sh`. It uses an existing `idf.py`, `$IDF_PATH/export.sh`, `~/esp/esp-idf-v5.3.5/export.sh`, or `~/esp/esp-idf/export.sh`, then runs:

```bash
idf.py set-target esp32c3
idf.py build
```

It does not run `flash`, `monitor`, or any serial-port command.

The validated local command was:

```bash
IDF_PATH=/Users/vinson/esp/esp-idf-v5.3.5 ./scripts/build_firmware.sh
```

Result:

```text
Project build complete.
xob_esp32c3.bin binary size 0xe22b0 bytes.
Smallest app partition is 0x380000 bytes.
0x29dd50 bytes (75%) free.
```

ESP-IDF generates `firmware/esp32c3/sdkconfig` and `firmware/esp32c3/build/` locally. They are ignored and not committed.

## CI Status

GitHub firmware CI is deferred because the current GitHub OAuth credential cannot create workflow files without the `workflow` scope.

## Check

Local repository checks:

```bash
bash -n scripts/build_firmware.sh
python3 scripts/check_firmware_skeleton.py
IDF_PATH=/Users/vinson/esp/esp-idf-v5.3.5 ./scripts/build_firmware.sh
```

Expected output:

```text
check_firmware_skeleton ok
```

Do not flash until the restore path and target board are reviewed again.
