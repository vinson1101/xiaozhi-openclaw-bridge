# xiaozhi-openclaw-bridge

Custom ESP32 voice terminal and bridge for OpenClaw, Hermas, and Zebra-backed agents.

## 0. Project Positioning

This project turns a Xiaozhi-style ESP32 voice device into a private voice terminal for remote agents.

The target path does not depend on the official Xiaozhi cloud service. If needed, the board can be flashed with custom firmware.

The board should handle wake/listen/play/display and only lightweight local context. The server should handle ASR, TTS, routing, agent execution, durable memory, tools, and audit.

Project documents:

- [docs/DESIGN.md](docs/DESIGN.md): basic product design
- [docs/TECHNICAL_ROUTE.md](docs/TECHNICAL_ROUTE.md): technical route
- [docs/PHASE0_HARDWARE.md](docs/PHASE0_HARDWARE.md): public hardware baseline
- [docs/PHASE1_TEXT_BRIDGE.md](docs/PHASE1_TEXT_BRIDGE.md): text bridge MVP
- [docs/PHASE2_OPENCLAW_ADAPTER.md](docs/PHASE2_OPENCLAW_ADAPTER.md): OpenClaw SSH adapter
- [docs/PHASE4_DEVICE_HTTP.md](docs/PHASE4_DEVICE_HTTP.md): device HTTP simulator
- [docs/PHASE5_HARDWARE_RECON.md](docs/PHASE5_HARDWARE_RECON.md): hardware recon before firmware
- [docs/PHASE6_FIRMWARE_SKELETON.md](docs/PHASE6_FIRMWARE_SKELETON.md): ESP32-C3 firmware skeleton
- [docs/PHASE6B_WIFI_HELLO.md](docs/PHASE6B_WIFI_HELLO.md): ESP32-C3 WiFi and device hello
- [docs/PHASE6C_EYE_RENDER.md](docs/PHASE6C_EYE_RENDER.md): ST7789-ready eye render commands
- [docs/PHASE6D_SERIAL_PROVISIONING.md](docs/PHASE6D_SERIAL_PROVISIONING.md): USB serial provisioning
- [docs/PHASE6E_BUILD_VALIDATION.md](docs/PHASE6E_BUILD_VALIDATION.md): no-flash ESP-IDF build validation
- [docs/PHASE6G_FIRST_FLASH_REVIEW.md](docs/PHASE6G_FIRST_FLASH_REVIEW.md): first-flash and restore review
- [docs/PHASE6H_BOOT_PIN_RECON.md](docs/PHASE6H_BOOT_PIN_RECON.md): sanitized boot log and pin recon
- [docs/M5STACK_REFERENCE.md](docs/M5STACK_REFERENCE.md): M5Stack borrowing boundary
- [docs/PROVISIONING.md](docs/PROVISIONING.md): configuration and provisioning plan
- [PLAN.md](PLAN.md): milestone plan

## 1. Current Hardware Baseline

Observed board:

- Chip: ESP32-C3 QFN32 rev v0.4
- Flash: 8 MB
- USB: native USB-Serial/JTAG
- MAC: recorded locally, not published
- Stock firmware project: `xiaozhi`
- Stock firmware version: `1.6.1`
- Board/firmware type: `zuowei-c3-realtime-lcd`
- Display driver string observed: `ST7789`
- Stock endpoint observed: `mqtt.xiaozhi.me`

The ESP32-C3 is treated as a constrained endpoint, not as the main agent runtime.

## 2. Target Architecture

```text
Custom board firmware
  microphone / speaker / screen / buttons
  voice capture / playback / animated eyes avatar UI
        |
        v
Voice bridge service
  device protocol adapter
  ASR / TTS adapter
  auth and session routing
  local session and memory layer
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
- Do not depend on the official Xiaozhi cloud path for the target product.
- Keep OpenClaw, Hermas, and Zebra as server-side services.
- Use the board as an input/output device with small local state.
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

1. Text bridge: accept a text command and forward it to a backend.
2. OpenClaw adapter: forward commands through a private SSH/CLI path.
3. Agent adapter: normalize OpenClaw, Hermas, and Zebra session responses.
4. Firmware backup: keep a restorable copy of the stock board flash.
5. Custom firmware skeleton: WiFi, secure config, display states, bridge hello.
6. Voice loop: ASR command in, agent result out, TTS response back.

## 6. References

- `memovai/mimiclaw`: ESP32-S3 OpenClaw-style firmware reference.
- `78/xiaozhi-esp32`: Xiaozhi firmware and board/protocol reference.
- `huangjunsen0406/py-xiaozhi`: Python Xiaozhi client/server-side reference.
- `hellolukeding/zebra`: server-side Codex-like agent architecture reference.
