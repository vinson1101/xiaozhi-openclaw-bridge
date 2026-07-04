# Phase 7E XiaoZhi WebSocket Handshake

Phase 7E aligns the main device channel with the upstream XiaoZhi protocol
instead of extending the temporary HTTP audio upload path.

## Reference

The upstream XiaoZhi firmware opens the audio channel only when needed, connects
to a WebSocket URL, sends headers such as `Authorization`, `Protocol-Version`,
`Device-Id`, and `Client-Id`, then sends a JSON `hello` message. The server must
reply with `type: "hello"` and `transport: "websocket"`. Later traffic uses JSON
control messages and binary Opus audio frames on the same connection.

## Current Slice

The Bridge accepts:

```text
GET /device/ws
```

with a WebSocket upgrade request. The first supported text frame is:

```json
{
  "type": "hello",
  "version": 1,
  "transport": "websocket",
  "audio_params": {
    "format": "opus",
    "sample_rate": 16000,
    "channels": 1,
    "frame_duration": 60
  }
}
```

The Bridge replies:

```json
{
  "type": "hello",
  "transport": "websocket",
  "session_id": "...",
  "audio_params": {
    "format": "opus",
    "sample_rate": 24000,
    "channels": 1,
    "frame_duration": 60
  }
}
```

Device token handling follows the existing pairing rule. The raw token is not
stored in SQLite.

## Boundary

This phase does not stream audio yet. It only proves the server-side XiaoZhi
WebSocket handshake. HTTP `/device/audio` remains a diagnostic probe, not the
final voice protocol.

Firmware exposes a serial `:ws` probe for the same handshake. It supports the
current plain `http://` Bridge URL path only; `wss://` is deferred until the
firmware has a TLS WebSocket client path.

Firmware also exposes `:status` as a safe diagnostic command. It reports only
whether config values exist, the HTTP port, and a non-secret host hash. It does
not print the raw Bridge URL, token, WiFi SSID, password, device id, or MAC.

If `:ws` reports `HTTP/1.0 404 Not Found` or another non-101 status, WiFi and
basic `/device/hello` can still be healthy. That failure means the configured
public entrypoint does not currently route `/device/ws` to the WebSocket-capable
Bridge service.

## Validation

```bash
python3 scripts/check_xiaozhi_ws_handshake.py
python3 scripts/check_firmware_skeleton.py
```

Expected output:

```text
check_xiaozhi_ws_handshake ok
```
