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
- Volume down and volume up held together for two seconds while running enter
  provisioning.
- The middle button follows the upstream XiaoZhi toggle shape: short press
  starts auto listening from idle, and a second press during an active voice
  session requests stop/interrupt.
- Middle long-press is treated as hardware power behavior and is not assigned a
  firmware long-press action.
- Button mask changes are logged as safe GPIO bitmasks for physical mapping
  checks.
- Volume buttons adjust an in-memory volume value in 5-point steps.

The volume value is not persisted yet. Phase 7 playback now uses it when sending
compatible returned WAV/PCM audio to VB6824.

## Boundaries

- No full NVS erase.
- No stock configuration reads.
- Server-side cancel is still pending; the current stop request only affects
  the local capture loop.
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

## Known Bug

Short-pressing the physical middle button does not visibly change the avatar
state on the current flashed board, even though GPIO 7/8/9 initialize as inputs.

Likely causes to verify on the next flash:

- the physical middle button is not GPIO8,
- the press is handled by board power circuitry before firmware sees it,
- a later state transition overwrites the listening frame too quickly.

This is deferred because it does not block Bridge command or audio-path work.
