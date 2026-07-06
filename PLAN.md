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
- Firmware serial text command exists in `docs/PHASE6T_SERIAL_TEXT_COMMAND.md`.
- Firmware command routing uses `xob.default_target`, falling back to `fake`.
- Firmware serial text command mode can enter provisioning with `:config` or `:setup`.
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
- ESP-IDF build passed locally; `xob_esp32c3.bin` size is `0xfa390`, with 72% of the smallest app partition free.
- GitHub firmware CI is deferred until the GitHub credential has `workflow` scope.
- M5Stack reference boundary exists in `docs/M5STACK_REFERENCE.md`.
- Provisioning plan exists in `docs/PROVISIONING.md`.
- Custom firmware has been flashed once with the reviewed non-erase path; the stock full-flash backup remains local and ignored by Git.
- Public GitHub remote exists at `https://github.com/vinson1101/xiaozhi-openclaw-bridge`.
- Systemd Bridge service template exists in `deploy/systemd/xob-bridge.service`.
- Bridge HTTP request logs print method, path, and status only.
- Bridge health check script exists in `scripts/check_bridge_health.py`.
- SQLite backup/restore helper exists in `scripts/bridge_db_backup.py`.
- Nginx reverse proxy sample exists in `deploy/nginx/xob-bridge.conf`.
- Deployment service requires device tokens for pairing.
- Offline device token rotation helper exists in `scripts/rotate_device_credential.py`.
- Device HTTP simulator validates reconnect with the existing session id.
- Xiaoyuan fixed voice prompts exist in `src/xiaozhi_openclaw_bridge/voice.py`.
- ASR provider boundary exists with a local fake provider.
- TTS provider boundary exists with cacheable fixed prompts and a local fake WAV provider.
- MiniMax TTS provider exists as an opt-in dynamic-answer provider using the same MiniMax API key already configured for OpenClaw. The key is not committed; VPS Bridge reads it from `/etc/xob-bridge/minimax.env`.
- VPS Bridge is now running with `XOB_TTS_PROVIDER=minimax`. A live MiniMax test returned a 29,858-byte `audio/wav` file with a `RIFF` header for the text `小元`.
- Recognized text can be routed through the existing Bridge command handler.
- Bridge accepts paired device audio uploads at `/device/audio` and routes fake ASR text through the command handler.
- Firmware serial `:voice` / `:audio` probe uploads silent PCM to `/device/audio`.
- Bridge accepts XiaoZhi-compatible WebSocket hello at `/device/ws`.
- Bridge accepts XiaoZhi-style WebSocket `listen` controls and binary audio frames, then returns `stt` / `tts` text frames plus one or more raw PCM TTS audio frames.
- Firmware serial `:ws` probe performs the XiaoZhi-compatible WebSocket hello handshake.
- Firmware serial `:talk` probe sends a XiaoZhi-style listen/audio/stop cycle and waits for `stt` / `tts` text frames plus returned TTS audio bytes.
- Firmware identifies the board microphone input as VB6824 UART on GPIO20/GPIO10 at 2 Mbps.
- Firmware serial `:vb` validates real VB6824 audio and wake-word frames from the board.
- Firmware serial `:vb-talk` sends real VB6824 Opus frames over `/device/ws` and receives `stt` / `tts` responses plus returned TTS audio bytes.
- Firmware middle-button short press now mirrors upstream XiaoZhi `ToggleChatState()`: idle starts `listen/start` with `mode:auto`, and another press during an active voice session requests stop/interrupt.
- Firmware starts a VB6824 wake listener after Bridge hello and maps offline command frames containing `小元` or `小智` to the listening/WebSocket voice path. The flashed board currently reports VB6824 configured wake word `你好小智`; real `你好，小元` wake requires a Xiaoyuan VB6824 voice-pack/wake-word update and rerun validation.
- Firmware serial `:vb-ota <code>` starts the official VB6824 OTA path for a voice-pack authorization code, using `0x0205` / `0x0105`, `jl_ota_start()`, and `jl_ondata()`.
- Board-side wake test with macOS Chinese TTS now validates the stock `你好小智` MVP path end to end through the reachable VPS: VB6824 wake command, WebSocket hello/listen/audio/stop, `stt`, returned fake WAV TTS, VB6824 playback write, and `websocket talk probe complete`. User human-speech testing still did not wake the device, so spoken wake reliability remains a tuning/voice-pack issue rather than a VPS connectivity issue.
- Firmware keeps the fixed 150-frame, about 3-second VB6824 capture only for the `:vb-talk` serial probe. Button and wake paths now use a 3000-frame safety cap plus early stop when VB6824 audio frames go idle.
- Original XiaoZhi/DOIT VB6824 board source confirms the wake gate is the VB6824 voice chip: board code compares received commands to `vb6824_get_wakeup_word()` and only then calls `WakeWordInvoke("你好小智")`. The ESP32 path can consume a Xiaoyuan command frame, but the voice pack must first make human-spoken `你好，小元` produce that frame.
- No public `你好小元` VB6824 authorization code has been found. MVP keeps the stock `你好小智` wake phrase so the voice loop can continue; Xiaoyuan remains the product persona and later voice-pack target.
- Firmware now plays returned WebSocket TTS audio as 16 kHz mono 16-bit PCM, sending queued PCM chunks to VB6824 with `0x2081` frames.
- Current Bridge code is deployed to the reachable VPS `xob-bridge` service. Board serial `:vb-talk` validates the path through VPS WebSocket, fake ASR, fake backend, binary fake WAV TTS, and VB6824 playback write: `websocket talk probe complete`.
- Fake TTS now returns a short non-silent 16 kHz mono WAV tone instead of silence. The updated Bridge is deployed to VPS, and a Mac-played `你好小智` wake test validates that the board receives and writes 3200 PCM bytes to VB6824 playback.
- OpenClaw on the VPS is installed and exposes four agents, including `huntmind`. Root CLI and the Bridge service path can run `openclaw agent --agent huntmind --message ... --session-key ...`; a two-turn smoke test confirms session continuity.
- The deployed Bridge service maps `target=huntmind` through a restricted local wrapper, so the public device host still exposes only the Bridge, not OpenClaw itself.
- Firmware serial `:target <agent>` updates only `xob.default_target` and reboots. The AP provisioning page treats `default_target` as a free-form route name, not as a WiFi choice.
- The flashed board has `default_target=huntmind`. The earlier fake-ASR transport loop was superseded by the Bailian ASR validation below.
- ASR now has an opt-in OpenAI provider path that wraps HTTP PCM16 as WAV and VB6824 WebSocket Opus frames as Ogg Opus before transcription. It is not enabled on the VPS because `OPENAI_API_KEY` is not configured.
- ASR now also has an opt-in Alibaba Cloud Bailian/DashScope `fun-asr-flash-2026-06-15` provider path. The user's speech-model quota covers ASR and TTS models, so this is the first real ASR bring-up provider while MiniMax remains the currently deployed dynamic TTS provider.
- VPS Bridge is running with `XOB_ASR_PROVIDER=bailian_fun_flash`; the API key is stored only in `/etc/xob-bridge/bailian-asr.env`, not in Git.
- Board serial `:vb-talk` now validates the real chain through VPS: VB6824 Opus frames, Bailian Fun-ASR-Flash transcription, OpenClaw `huntmind`, MiniMax WAV TTS, server-side WAV-to-PCM framing, and VB6824 PCM playback. The latest controlled run transcribed `测试成功。`, called `huntmind`, wrote 126,690 PCM bytes to VB6824, and ended with `websocket talk probe complete`.
- Bridge strips MiniMax WAV headers and sends raw 16 kHz mono PCM WebSocket audio frames for the current firmware playback path. In voice mode, the OpenClaw adapter asks the agent to generate a short spoken answer at the source instead of hard-truncating long Markdown output.
- Audio playback follows the original XiaoZhi/DOIT pipeline shape: WebSocket receive enqueues returned audio, while a dedicated playback path feeds VB6824. Do not pace speaker output directly from WebSocket binary frame boundaries.
- Product target is the original XiaoZhi voice experience: short-press toggle, `mode:auto`, automatic VAD/endpointer, interrupt while speaking/listening, and streamed/low-latency response. Non-streaming transcription remains acceptable only as a bring-up fallback, not the final interaction model.
- Firmware serial `:status` reports safe Bridge endpoint diagnostics without printing raw secrets.
- Firmware provisioning can keep existing non-empty values when fields are left blank.
- Deployment units disable the generic `/command` route for public device hosts.
- Real board `/device/hello` is validated against a reachable token-protected VPS Bridge.
- Real board serial text command reaches `/device/command` on the reachable Bridge.

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
- [x] Validate one live non-destructive `openclaw agent --json` call after operator policy is confirmed.

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

Phase 4A uses HTTP JSON first. WebSocket is now the preferred voice-session path
because it matches XiaoZhi's upstream server protocol.

Tasks:

- [x] Add WebSocket `/device/ws` hello handshake.
- [x] Implement HTTP `hello` and `command.text`.
- [x] Add simulator script.
- [x] Store device pairing in SQLite.
- [x] Add reconnect behavior to simulator.

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
- [x] Identify microphone input path as VB6824 UART.
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
- [x] Add USB serial text command to `POST /device/command`.
- [x] Send configured `default_target` with board-side text commands.
- [x] Add serial escape from text command mode into AP/serial provisioning.
- [x] Allow provisioning to update only changed fields while keeping existing WiFi credentials.
- [x] Add `:target <agent>` so agent route can change without touching WiFi/token.
- [x] Make AP provisioning accept a free-form `default_target` route such as `huntmind`.
- [x] Validate one real `/device/hello` against a reachable Bridge.

Acceptance:

- Board boots custom firmware.
- Screen shows WiFi, Bridge state, and animated eyes.
- Bridge logs device `hello`.

## Phase 7 - Voice Loop

Goal: complete the first real voice command loop.

Tasks:

- [x] Align middle-button behavior with upstream XiaoZhi toggle semantics instead of press-to-record.
- [x] Upload real microphone frames through the WebSocket probe.
- [x] Convert the serial microphone probe into a middle-button voice probe.
- [x] Keep fixed 3-second capture only for the `:vb-talk` debug probe.
- [x] Add temporary button/wake auto endpoint based on VB6824 audio-frame idle.
- [ ] Replace temporary endpoint with real server-side VAD/endpointer after Opus decode/ASR is connected.
- [x] Add XiaoZhi-compatible WebSocket hello handshake.
- [x] Add firmware serial probe for the WebSocket hello handshake.
- [x] Add ASR provider adapter.
- [x] Route recognized text to backend.
- [x] Define fixed Xiaoyuan voice-pack prompts for wake, confirm, interrupt, error, and setup states.
- [x] Add VB6824 offline command listener for Xiaoyuan wake frames.
- [x] Validate that macOS-played stock `你好小智` wakes the flashed board and completes the VPS voice loop.
- [ ] Validate that human-spoken `你好小智` wakes the flashed board on the MVP path.
- [x] Add VB6824 voice-pack OTA serial entry for a Xiaoyuan authorization code.
- [ ] Later: obtain or generate a VB6824 voice-pack code that recognizes `你好，小元`.
- [x] Add opt-in MiniMax TTS as the first dynamic-answer provider; avoid stiff generic TTS except as fallback.
- [x] Configure VPS Bridge service with the existing OpenClaw MiniMax key and validate one live MiniMax WAV response.
- [x] Add TTS provider adapter with cacheable fixed prompts and replaceable dynamic provider.
- [x] Receive returned TTS audio bytes on device.
- [x] Add first board-side returned WAV/PCM playback path through VB6824.
- [x] Validate returned-audio playback write on the flashed board with fake silent WAV.
- [x] Replace fake silent WAV with a short non-silent WAV tone and validate board playback write through VPS.
- [ ] Validate audible returned-audio playback with non-silent TTS.
- [x] Configure Bridge service to route `target=huntmind` through a restricted OpenClaw CLI wrapper.
- [x] Switch flashed board `default_target` from `fake` to `huntmind`.
- [x] Validate board `:vb-talk` through VPS Bridge to OpenClaw `huntmind` and returned WAV playback write.
- [x] Add opt-in OpenAI ASR provider path for PCM16/WAV and VB6824 Opus-frame input.
- [x] Add opt-in Alibaba Cloud Bailian Fun-ASR-Flash provider path for the first real ASR bring-up.
- [x] Configure VPS Bridge with Bailian Fun-ASR-Flash and validate real spoken Chinese from the board.
- [x] Chunk returned MiniMax WAV into firmware-friendly WebSocket frames and validate VB6824 PCM playback with real agent output.
- [x] Add VB6824 playback queue so WebSocket receive is decoupled from fixed-cadence UART playback.
- [x] Add voice-mode OpenClaw prompt shaping so spoken replies are short plain Chinese instead of long Markdown sent to TTS.
- [ ] Add Opus decode plus VAD/endpointer on the Bridge, matching XiaoZhi auto-stop behavior.
- [ ] Configure a streaming-capable ASR provider or run the original XiaoZhi server ASR stack, then validate real spoken Chinese from the board.
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

- [x] Add pairing flow.
- [x] Add token rotation.
- [x] Add reverse proxy sample.
- [x] Add service unit or container deployment.
- [x] Add health checks.
- [x] Add backup/restore for SQLite.
- [x] Add logging without secret leakage.

Acceptance:

- Device connects to `wss://...`.
- Bridge can restart without losing session state.
- Public internet does not expose OpenClaw/Hermas/Zebra directly.

## Next Task

Continue Phase 7 voice-loop work against the reachable Bridge host. The
transport loop to OpenClaw `huntmind` is proven, but the ASR text is still fake.
Next priority is real VB6824 Opus decode/transcription, then audible dynamic
TTS quality. The current Mac LAN path remains blocked by WiFi client-to-client
reachability, so do not use the Mac LAN address as the board Bridge target
unless router isolation changes.

Do not store WiFi passwords, device tokens, VPS connection strings, flash backups, or raw device identifiers in Git.
