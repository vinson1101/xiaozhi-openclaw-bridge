# Phase 7D Voice Text Route

This slice proves that recognized text can enter the existing Bridge command
route.

## Scope

Local composition only:

```text
fake ASR -> recognized text -> /command handler -> backend adapter
```

The route stores the same `command.received` and `backend.response` events as
normal text commands.

## Boundary

This phase does not add a new public HTTP endpoint, WebSocket audio upload,
board-side recording, or real ASR. Device audio transport is still a later
Phase 7 task.

## Validation

```bash
python3 scripts/check_voice_route.py
```

Expected output:

```text
check_voice_route ok
```
