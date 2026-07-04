# Phase 6T Serial Text Command

Phase 6T adds the first board-side command path after WiFi.

## Result

After WiFi connects, the firmware starts a USB Serial/JTAG text input task:

```text
XOB serial text command ready. Type text and press Enter.
```

Typing one line sends:

```text
POST /device/command
```

The payload includes the board `device_id` and the typed `text`. The Bridge
keeps its existing default target behavior, so no new device protocol was added.

The avatar switches to:

- `thinking` while the command is being sent
- `speaking` if the Bridge returns 2xx
- `error` if the request fails

## Boundaries

- No response JSON parsing yet.
- No screen text rendering yet.
- No audio recording or playback.
- No new dependency.

## Known Blocker

On the current home WiFi, the board still cannot reach the Mac Bridge host, so
real command success needs a reachable Bridge host first.

## Board Check

This firmware was flashed with the reviewed non-erase write path. Sanitized
serial evidence showed:

```text
XOB serial text command ready. Type text and press Enter.
device command failed: ESP_ERR_HTTP_CONNECT
```

That proves the board-side serial input task can trigger `/device/command`.
The failure is the known Bridge reachability issue, not command-task startup.
