# Phase 6P Bridge Status Display

Phase 6P adds a lightweight on-screen state layer for the custom firmware.

## Scope

- Keep the animated avatar as the primary screen.
- Add two small bottom status markers:
  - left marker: WiFi state
  - right marker: Bridge state
- Move the horizontal mouth line slightly lower so speaking animation has more room.
- Drive eye state from lifecycle:
  - provisioning: listening
  - WiFi connecting: thinking
  - WiFi failed: error
  - Bridge hello pending: thinking
  - Bridge hello failed: error
  - Bridge hello complete: idle

Status colors:

| State | Color |
|---|---|
| off | dim gray |
| pending | amber |
| ok | green |
| error | red |

## Build Check

Validated locally:

```bash
python3 scripts/check_eye_render.py
python3 scripts/check_firmware_skeleton.py
python3 scripts/check_flash_backup.py
./scripts/build_firmware.sh
```

Build result:

```text
xob_esp32c3.bin binary size 0xee410 bytes
Smallest app partition is 0x380000 bytes
0x291bf0 bytes (73%) free
```

## Deferred

This phase does not add command polling, WebSocket, audio, ASR, or TTS.

The next useful step is Bridge-side durable device pairing, then provisioning the board against a local Bridge to validate a real `/device/hello`.
