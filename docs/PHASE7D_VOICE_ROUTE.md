# Phase 7D Voice Text Route

This slice proves that device audio can enter ASR and then reuse the existing
Bridge command route.

## Scope

Route:

```text
POST /device/audio -> ASR -> recognized text -> command handler -> backend adapter
```

The request body is binary PCM/WAV-style audio bytes. Device id, target, sample
rate, channels, and language are query parameters:

```text
/device/audio?device_id=<id>&target=fake&sample_rate=16000&channels=1
```

The route requires the existing device pairing token when the device was paired
with one. Raw audio is not stored in SQLite; the Bridge records byte count,
sample rate, channel count, ASR status, and ASR summary, then stores the same
`command.received` and `backend.response` events as normal text commands.

## Boundary

This phase does not add WebSocket streaming, board-side recording, playback, or
real ASR. The current endpoint is enough for the next firmware step: upload one
short PCM16 recording.

## Validation

```bash
python3 scripts/check_voice_route.py
```

Expected output:

```text
check_voice_route ok
```
