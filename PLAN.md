# Plan

## Current Status

- Project shell exists under `/Users/vinson/Projects/github/vinson1101/xiaozhi-openclaw-bridge`.
- Git is initialized on `main`.
- Basic design exists in `docs/DESIGN.md`.
- Technical route exists in `docs/TECHNICAL_ROUTE.md`.
- Text Bridge MVP exists in `docs/PHASE1_TEXT_BRIDGE.md`.
- No firmware has been flashed for this project.
- Public GitHub remote exists at `https://github.com/vinson1101/xiaozhi-openclaw-bridge`.

## Phase 0 - Design And Safety Baseline

Goal: make the project safe to continue.

Tasks:

- [x] Create Git-backed project directory.
- [x] Write basic product design.
- [x] Write technical route.
- [x] Record current board identity and port.
- [x] Create full flash backup for the ESP32-C3 board.
- [x] Verify restore command using backup metadata, without restoring unless needed.

Acceptance:

- Design documents are committed.
- Flash backup path, SHA256, chip, flash size, and MAC are recorded outside Git or in Git with no binary backup.

## Phase 1 - Text Bridge MVP

Goal: prove that a command can reach an agent backend without touching firmware.

Tasks:

- [ ] Add minimal Python project skeleton.
- [x] Add minimal Python project skeleton.
- [x] Add `/healthz`.
- [x] Add `POST /command`.
- [x] Add fake backend adapter.
- [x] Add SQLite session/event store.
- [x] Add one command-line smoke test.

Acceptance:

```text
POST /command {"target":"fake","text":"让龙虾检查今天任务状态"}
  -> returns Chinese text
  -> writes one session
  -> writes ordered session events
```

## Phase 2 - OpenClaw Adapter

Goal: connect the Bridge to the real OpenClaw path.

Tasks:

- [ ] Identify the safest OpenClaw entrypoint: HTTP first, CLI fallback.
- [ ] Implement `OpenClawAdapter`.
- [ ] Normalize OpenClaw result into Bridge response.
- [ ] Preserve `needs_approval` instead of auto-executing risky actions.
- [ ] Add fake fixture test for OpenClaw response parsing.

Acceptance:

- Bridge can call OpenClaw in a non-destructive mode.
- Errors are returned as structured Bridge responses.
- No OpenClaw credentials are stored in Git.

## Phase 3 - Hermas Adapter

Goal: reserve the same contract for Hermas without blocking earlier work.

Tasks:

- [ ] Confirm Hermas service API.
- [ ] Implement `HermasAdapter`.
- [ ] Normalize status, text, summary, and artifacts.
- [ ] Add one smoke test or fixture test.

Acceptance:

- Hermas can be selected by `target:"hermas"` using the same `/command` API.

## Phase 4 - Device Protocol And Simulator

Goal: prove the device protocol before flashing hardware.

Tasks:

- [ ] Add WebSocket `/device`.
- [ ] Implement `hello`, `state`, `command.text`.
- [ ] Add simulator script.
- [ ] Store device pairing in SQLite.
- [ ] Add reconnect behavior to simulator.

Acceptance:

```text
simulator -> hello -> command.text -> Bridge -> backend -> state/result
```

## Phase 5 - Firmware Backup And Hardware Recon

Goal: prepare for safe flashing.

Tasks:

- [ ] Backup full 8 MB flash.
- [ ] Record partition table.
- [ ] Record boot log.
- [ ] Identify display init path.
- [ ] Identify buttons, microphone, speaker/audio codec, and battery ADC where possible.
- [ ] Decide whether current ESP32-C3 board is enough or whether ESP32-S3 is required.

Acceptance:

- Restore path is documented.
- Hardware unknowns are listed explicitly.
- No firmware write happens before backup exists.

## Phase 6 - Custom Firmware Skeleton

Goal: bring up a private firmware without audio first.

Tasks:

- [ ] Create ESP-IDF firmware workspace.
- [ ] Implement WiFi config.
- [ ] Implement NVS config for Bridge URL and token.
- [ ] Implement ST7789 status screen.
- [ ] Implement animated eyes MVP: static eyes, blinking, state changes.
- [ ] Implement WebSocket `hello`.
- [ ] Display Bridge connection state.

Acceptance:

- Board boots custom firmware.
- Screen shows WiFi, Bridge state, and animated eyes.
- Bridge logs device `hello`.

## Phase 7 - Voice Loop

Goal: complete the first real voice command loop.

Tasks:

- [ ] Add press-to-record.
- [ ] Upload PCM16 audio frames.
- [ ] Add ASR provider adapter.
- [ ] Route recognized text to backend.
- [ ] Add TTS provider adapter.
- [ ] Play returned audio on device.
- [ ] Display recognized text and final answer summary.
- [ ] Drive eye states from audio lifecycle: listening, thinking, speaking, error.

Acceptance:

```text
press button
  -> speak command
  -> Bridge transcribes
  -> OpenClaw/Hermas responds
  -> board speaks answer
  -> screen shows short result
```

## Phase 8 - Zebra Runtime Integration

Goal: reuse Zebra architecture on the server side where it belongs.

Tasks:

- [ ] Confirm current Zebra HTTP/session API.
- [ ] Implement `ZebraAdapter`.
- [ ] Map Bridge sessions to Zebra sessions.
- [ ] Stream or poll Zebra events.
- [ ] Preserve approval and policy boundaries.
- [ ] Store Zebra artifacts in Bridge artifact records.

Acceptance:

- `target:"zebra"` creates or resumes a Zebra session.
- Zebra events can be summarized back to the voice device.

## Phase 9 - Deployment Hardening

Goal: make VPS mode safe enough for real use.

Tasks:

- [ ] Add pairing flow.
- [ ] Add token rotation.
- [ ] Add reverse proxy sample.
- [ ] Add service unit or container deployment.
- [ ] Add health checks.
- [ ] Add backup/restore for SQLite.
- [ ] Add logging without secret leakage.

Acceptance:

- Device connects to `wss://...`.
- Bridge can restart without losing session state.
- Public internet does not expose OpenClaw/Hermas/Zebra directly.

## Next Task

Start with Phase 1:

```text
Text Bridge MVP
```

Do not start firmware work until Phase 1-4 pass and Phase 5 backup is complete.
