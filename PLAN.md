# Plan

## Current Status

- Project shell exists under `/Users/vinson/Projects/github/vinson1101/xiaozhi-openclaw-bridge`.
- Git is initialized on `main`.
- Basic design exists in `docs/DESIGN.md`.
- Technical route exists in `docs/TECHNICAL_ROUTE.md`.
- Text Bridge MVP exists in `docs/PHASE1_TEXT_BRIDGE.md`.
- OpenClaw SSH adapter exists in `docs/PHASE2_OPENCLAW_ADAPTER.md`.
- Device HTTP simulator exists in `docs/PHASE4_DEVICE_HTTP.md`.
- Durable device pairing exists in `docs/PHASE4B_DEVICE_PAIRING.md`.
- Hardware recon exists in `docs/PHASE5_HARDWARE_RECON.md`.
- Firmware skeleton exists in `docs/PHASE6_FIRMWARE_SKELETON.md`.
- Firmware WiFi hello exists in `docs/PHASE6B_WIFI_HELLO.md`.
- Firmware eye render commands exist in `docs/PHASE6C_EYE_RENDER.md`.
- Firmware USB serial provisioning exists in `docs/PHASE6D_SERIAL_PROVISIONING.md`.
- Firmware AP provisioning exists in `docs/PHASE6R_AP_PROVISIONING.md`.
- Firmware three-button controls exist in `docs/PHASE6S_BUTTON_CONTROLS.md`.
- No-flash ESP-IDF build path exists in `docs/PHASE6E_BUILD_VALIDATION.md`.
- First-flash and restore review exists in `docs/PHASE6G_FIRST_FLASH_REVIEW.md`.
- Sanitized boot log and pin recon exists in `docs/PHASE6H_BOOT_PIN_RECON.md`.
- LCD pin map search and probe gate exists in `docs/PHASE6I_LCD_PROBE_GATE.md`.
- Open C3 reference board check exists in `docs/PHASE6J_OPEN_C3_REFERENCES.md`.
- Photo-based hardware recon exists in `docs/PHASE6K_PHOTO_HARDWARE_RECON.md`.
- External source review exists in `docs/PHASE6L_EXTERNAL_SOURCE_REVIEW.md`.
- Stock binary LCD pin recon exists in `docs/PHASE6M_STOCK_BINARY_LCD_PIN_RECON.md`.
- ST7789 LCD driver exists in `docs/PHASE6N_LCD_DRIVER.md`.
- First custom firmware flash and avatar check exists in `docs/PHASE6O_FIRST_CUSTOM_FLASH.md`.
- WiFi and Bridge status display exists in `docs/PHASE6P_BRIDGE_STATUS_DISPLAY.md`.
- Local ESP-IDF `v5.3.5` is installed at `/Users/vinson/esp/esp-idf-v5.3.5`.
- ESP-IDF build passed locally; `xob_esp32c3.bin` size is `0xf9e70`, with 72% of the smallest app partition free.
- GitHub firmware CI is deferred until the GitHub credential has `workflow` scope.
- M5Stack reference boundary exists in `docs/M5STACK_REFERENCE.md`.
- Provisioning plan exists in `docs/PROVISIONING.md`.
- Custom firmware has been flashed once with the reviewed non-erase path; the stock full-flash backup remains local and ignored by Git.
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

- [x] Identify the safest OpenClaw entrypoint: CLI through private SSH.
- [x] Implement `OpenClawAdapter`.
- [x] Normalize OpenClaw result into Bridge response.
- [x] Preserve explicit command enablement instead of auto-executing by default.
- [x] Add fake fixture test for OpenClaw response parsing.
- [ ] Validate one live non-destructive `openclaw agent --json` call after operator policy is confirmed.

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

Phase 4A uses HTTP JSON first. WebSocket stays deferred until firmware consumes it.

Tasks:

- [ ] Add WebSocket `/device`.
- [x] Implement HTTP `hello` and `command.text`.
- [x] Add simulator script.
- [x] Store device pairing in SQLite.
- [ ] Add reconnect behavior to simulator.

Acceptance:

```text
simulator -> hello -> command.text -> Bridge -> backend -> state/result
```

## Phase 5 - Firmware Backup And Hardware Recon

Goal: prepare for safe flashing.

Tasks:

- [x] Backup full 8 MB flash.
- [x] Record partition table.
- [x] Run static recon against the local flash backup.
- [x] Record boot log.
- [x] Identify display init path at driver level.
- [x] Record photo-visible MCU, flash, amplifier, and battery markings.
- [ ] Identify exact microphone, speaker/audio codec, and remaining battery/power GPIO/pin map.
- [x] Infer LCD SPI, reset, and backlight pins from the stock binary.
- [x] Decide whether current ESP32-C3 board is enough for Phase 6 display/control.

Acceptance:

- Restore path is documented.
- Hardware unknowns are listed explicitly.
- No firmware write happens before backup exists.

## Phase 6 - Custom Firmware Skeleton

Goal: bring up a private firmware without audio first.

Tasks:

- [x] Create ESP-IDF firmware workspace.
- [x] Preserve stock-compatible partition layout.
- [x] Implement WiFi config.
- [x] Implement NVS config read path for Bridge URL and token.
- [x] Implement provisioning write path: USB serial first, temporary AP later.
- [x] Implement ST7789 status screen.
- [x] Implement avatar eye state model and blinking geometry.
- [x] Render avatar eyes as ST7789-ready RGB565 rectangles.
- [x] Add no-flash ESP-IDF build script.
- [x] Validate ESP-IDF build locally after ESP-IDF is installed or sourced.
- [x] Review first-flash command and restore checklist, without flashing.
- [x] Search for Zuowei LCD pin map and define LCD probe gate, without flashing.
- [x] Check open XiaoZhi C3 reference boards for reusable LCD pin clues.
- [x] Review external GMT154/Zuowei source clues and reject unverified pinout tables.
- [x] Infer stock LCD pin map from the local flash backup, without flashing.
- [x] Send avatar eye rectangles to ST7789 after LCD pins are inferred.
- [x] Validate ST7789 firmware build locally, without flashing.
- [x] Implement HTTP JSON `hello`.
- [x] Display Bridge connection state.
- [x] Add safe WiFi scan diagnostics and USB serial fallback after WiFi failure.
- [x] Provision WiFi config on the flashed board and validate WiFi association.
- [x] Add board-side gateway, internet, and Bridge-host reachability diagnostics.
- [x] Implement temporary AP plus local HTTP config page for normal WiFi/Bridge setup.
- [x] Add three-button provisioning entry plus interrupt/listen and volume placeholders.
- [ ] Validate one real `/device/hello` against a reachable Bridge.

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

Continue Phase 6Q after making a Bridge reachable from the board:

```text
provision the board against a local Bridge and validate a real /device/hello
```

If the board is available, flash the Phase 6R firmware and validate the AP setup
page before continuing Phase 6Q hello validation.

Do not store WiFi passwords, device tokens, VPS connection strings, flash backups, or raw device identifiers in Git.
