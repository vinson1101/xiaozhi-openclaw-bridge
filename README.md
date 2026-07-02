# xiaozhi-openclaw-bridge

Xiaozhi voice terminal bridge for OpenClaw, Hermas, and Zebra-backed agents.

## 0. Project Positioning

This project turns a Xiaozhi ESP32 voice device into a thin voice terminal for remote agents.

The board should handle wake/listen/play/display. The server should handle ASR, TTS, routing, agent execution, memory, tools, and audit.

## 1. Current Hardware Baseline

Observed board:

- Chip: ESP32-C3 QFN32 rev v0.4
- Flash: 8 MB
- USB: native USB-Serial/JTAG
- MAC: `b4:3a:45:52:d0:18`
- Stock firmware project: `xiaozhi`
- Stock firmware version: `1.6.1`
- Board/firmware type: `zuowei-c3-realtime-lcd`
- Display driver string observed: `ST7789`
- Stock endpoint observed: `mqtt.xiaozhi.me`

The ESP32-C3 is treated as a constrained endpoint, not as the agent runtime.

## 2. Target Architecture

```text
Xiaozhi board
  microphone / speaker / screen / buttons
  voice capture and playback only
        |
        v
Voice bridge service
  Xiaozhi protocol adapter
  ASR / TTS adapter
  auth and session routing
        |
        v
Agent services
  OpenClaw
  Hermas
  Zebra session runtime
```

## 3. Boundaries

- Do not port Zebra into ESP32 firmware.
- Do not vendor `memovai/mimiclaw` or `78/xiaozhi-esp32` source here by default.
- Keep OpenClaw, Hermas, and Zebra as server-side services.
- Use the board as an input/output device.
- Keep flash backups, credentials, API keys, and personal runtime data out of Git.

## 4. Zebra Migration Contract

Migrate Zebra's architecture as server-side concepts:

- session API
- append-only event store
- context compiler
- typed tool gateway
- policy and credential boundaries
- artifact/audit trail

Do not migrate Zebra's implementation into firmware.

## 5. First Milestones

1. Text bridge: accept a text command and forward it to OpenClaw or Hermas.
2. Agent adapter: normalize OpenClaw, Hermas, and Zebra session responses.
3. Xiaozhi protocol research: identify the smallest compatible WebSocket/MQTT path.
4. Voice loop: ASR command in, agent result out, TTS response back.
5. Firmware path: only if required, build a custom `zuowei-c3-realtime-lcd` firmware with a unique board identity.

## 6. References

- `memovai/mimiclaw`: ESP32-S3 OpenClaw-style firmware reference.
- `78/xiaozhi-esp32`: Xiaozhi firmware and board/protocol reference.
- `huangjunsen0406/py-xiaozhi`: Python Xiaozhi client/server-side reference.
- `hellolukeding/zebra`: server-side Codex-like agent architecture reference.
