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

with a WebSocket upgrade request. The optional query parameter
`target=<alias>` selects the backend route for audio-derived commands, matching
the existing `fake` / `openclaw` / `hermas` alias model. It defaults to `fake`.
The first supported text frame is:

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

After `hello`, the Bridge keeps the WebSocket open and supports the first
XiaoZhi-style session loop:

- device text frame `{"type":"listen","state":"start","mode":"manual"}`
- one or more binary audio frames
- device text frame `{"type":"listen","state":"stop"}`
- server text frames: `stt`, `tts/start`, `tts/sentence_start`, `tts/stop`

This is still a protocol slice. The binary frames are passed to the existing
local fake ASR provider as bytes; real Opus decode, streaming ASR, and outbound
TTS audio are later work.

Device token handling follows the existing pairing rule. The raw token is not
stored in SQLite.

## Boundary

This phase does not implement real Opus decode or outbound TTS audio yet. It
only proves the server-side XiaoZhi WebSocket control/audio frame shape. HTTP
`/device/audio` remains a diagnostic probe, not the final voice protocol.

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
