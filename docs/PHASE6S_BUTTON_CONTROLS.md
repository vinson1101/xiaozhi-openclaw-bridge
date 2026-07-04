# Phase 6S Button Controls

Phase 6S wires the three confirmed button GPIOs into the custom firmware.

## Mapping

The stock boot log confirms button inputs on GPIO 9, 8, and 7. The current
firmware uses this provisional physical mapping:

| Function | GPIO |
|---|---:|
| Volume down | 7 |
| Interrupt / listen placeholder | 8 |
| Volume up | 9 |

If real-board testing shows left/right or middle placement differs, only these
constants need to be swapped.

## Behavior

- All three buttons pressed at app startup enter provisioning.
- All three buttons held for two seconds while running enter provisioning.
- The middle button interrupts the current visual conversation state and sets the
  avatar to `listening` while held.
- Volume buttons adjust an in-memory volume value in 5-point steps.

The volume value is not persisted yet and is not connected to audio output.
That belongs to Phase 7 when playback exists.

## Boundaries

- No full NVS erase.
- No stock configuration reads.
- No audio recording, TTS stop, or server-side cancel starts yet.
- No long-press reset namespace erase yet.

## Validation

Validated in this phase:

```text
python3 scripts/check_firmware_skeleton.py
python3 scripts/check_eye_render.py
idf.py build
```

The firmware was also flashed with the reviewed non-erase write path. Sanitized
serial evidence showed:

```text
GPIO[7] input pull-up
GPIO[8] input pull-up
GPIO[9] input pull-up
ssid_or_bssid_log_present=false
```

Real physical button placement and press events remain pending on-board testing.
