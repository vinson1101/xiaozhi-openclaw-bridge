# Phase 4A Device HTTP Simulator

This slice proves the device-to-bridge path before firmware work.

## Scope

- `POST /device/hello`
- `POST /device/command`
- simulator smoke test
- event trail in the existing SQLite `session_events` table
- durable pairing in the SQLite `device_pairings` table
- reconnect by sending the previous `session_id` in the next `hello`

WebSocket is intentionally deferred until the firmware protocol needs it. The current project has no WebSocket dependency, and HTTP JSON is enough to validate routing.

## Contract

Hello:

```bash
curl -sS http://127.0.0.1:8788/device/hello \
  -H 'Content-Type: application/json' \
  -H 'Authorization: Bearer <device_token>' \
  -d '{"device_id":"sim-esp32-c3","firmware":"simulator","capabilities":["display","text"]}'
```

Command:

```bash
curl -sS http://127.0.0.1:8788/device/command \
  -H 'Content-Type: application/json' \
  -H 'Authorization: Bearer <device_token>' \
  -d '{"device_id":"sim-esp32-c3","session_id":"<session_id>","target":"fake","text":"你好，检查链路"}'
```

For local simulator-only development the token may be omitted. For real devices, provision a token; the Bridge stores only its SHA-256 hash.

Response shape:

```json
{
  "device_id": "sim-esp32-c3",
  "session_id": "...",
  "state": "result",
  "display": "short text for screen",
  "result": {
    "status": "done",
    "text": "...",
    "summary": "...",
    "artifacts": []
  }
}
```

## Smoke Test

```bash
python3 scripts/smoke_device_http.py
```

Expected output:

```text
smoke_device_http ok
```

## Deferred

- WebSocket `/device`
- binary audio frames

Add these when firmware starts consuming the protocol.
