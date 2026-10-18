# Plan

## Current Status

- Project shell exists under `<workspace>/xiaozhi-openclaw-bridge`.
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
- Local ESP-IDF `v5.3.5` is installed at `/path/to/esp-idf-v5.3.5` (example).
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
- VPS Bridge currently runs with `XOB_TTS_PROVIDER=bailian`, `XOB_TTS_STREAMING=1`,
  `XOB_BAILIAN_TTS_MODEL=cosyvoice-v3-flash`,
  `XOB_BAILIAN_TTS_VOICE=longyan_v3`, and
  `XOB_WS_TTS_AUDIO_CODEC=opus`. It splits spoken output into sentence/length
  segments (`XOB_TTS_SEGMENT_MAX_CHARS=60`) and keeps
  `XOB_TTS_SPOKEN_MAX_CHARS=240` only as a runaway safety cap. MiniMax remains
  available as a fallback provider but is not the active VPS voice-chain path.
- Recognized text can be routed through the existing Bridge command handler.
- Bridge accepts paired device audio uploads at `/device/audio` and routes fake ASR text through the command handler.
- Firmware serial `:voice` / `:audio` probe uploads silent PCM to `/device/audio`.
- Bridge accepts XiaoZhi-compatible WebSocket hello at `/device/ws`.
- Bridge accepts XiaoZhi-style WebSocket `listen` controls and binary audio frames, then returns `stt` / `tts` text frames plus one or more OPUS TTS audio packets.
- Firmware serial `:ws` probe performs the XiaoZhi-compatible WebSocket hello handshake.
- Firmware serial `:talk` probe sends a XiaoZhi-style listen/audio/stop cycle and waits for `stt` / `tts` text frames plus returned TTS audio bytes.
- Firmware identifies the board microphone input as VB6824 UART on GPIO20/GPIO10 at 2 Mbps.
- Firmware serial `:vb` validates real VB6824 audio and wake-word frames from the board.
- Firmware serial `:vb-talk` sends real VB6824 Opus frames over `/device/ws` and receives `stt` / `tts` responses plus returned TTS audio bytes.
- Firmware middle-button target UX is upstream XiaoZhi-style `ToggleChatState()`:
  idle starts `listen/start` with `mode:auto`; listening should normally end by
  VAD/endpointer, while another short press cancels/closes the current listening
  session back to idle; thinking/speaking short press aborts the current turn and
  should return to listening. Current firmware has a bring-up state machine for
  this, but listening-cancel and speaking-interrupt behavior still need board
  validation after the latest toggle-alignment change.
- Firmware starts a VB6824 wake listener after Bridge hello and maps offline command frames containing `小元` or `小智` to the listening/WebSocket voice path. The flashed board currently reports VB6824 configured wake word `你好小智`; real `你好，小元` wake requires a Xiaoyuan VB6824 voice-pack/wake-word update and rerun validation.
- Firmware serial `:vb-ota <code>` starts the official VB6824 OTA path for a voice-pack authorization code, using `0x0205` / `0x0105`, `jl_ota_start()`, and `jl_ondata()`.
- Board-side wake test with macOS Chinese TTS now validates the stock `你好小智` MVP path end to end through the reachable VPS: VB6824 wake command, WebSocket hello/listen/audio/stop, `stt`, returned fake WAV TTS, VB6824 playback write, and `websocket talk probe complete`. User human-speech testing still did not wake the device, so spoken wake reliability remains a tuning/voice-pack issue rather than a VPS connectivity issue.
- Button and wake listening now decode VB6824 Opus microphone frames for a real
  speech endpointer: wait for enough speech and stop after a silence tail.
  The normal end condition is the Opus-frame speech endpointer: currently about
  1.5 seconds of mostly silent tail. The 6000-frame limit is only a 120-second
  no-speech/runaway safety cap. Firmware keeps the fixed 150-frame, about
  3-second VB6824 capture only for the `:vb-talk` serial probe.
- Original XiaoZhi/DOIT VB6824 board source confirms the wake gate is the VB6824 voice chip: board code compares received commands to `vb6824_get_wakeup_word()` and only then calls `WakeWordInvoke("你好小智")`. The ESP32 path can consume a Xiaoyuan command frame, but the voice pack must first make human-spoken `你好，小元` produce that frame.
- No public `你好小元` VB6824 authorization code has been found. MVP keeps the stock `你好小智` wake phrase so the voice loop can continue; Xiaoyuan remains the product persona and later voice-pack target.
- Firmware now treats returned WebSocket TTS audio as OPUS when the server hello advertises `format=opus`, decodes it to 16 kHz mono 16-bit PCM on the ESP32-C3, then sends queued PCM chunks to VB6824 with `0x2081` frames. WAV/PCM fallback remains for debug compatibility.
- The earlier board serial `:vb-talk` fake-ASR/fake-TTS transport validation is
  historical. Current voice-loop validation should use the VPS path with Bailian
  ASR/TTS, OpenClaw `huntmind`, OPUS downlink, and VB6824 playback.
- Fake TTS now returns a short non-silent 16 kHz mono WAV tone instead of silence. The updated Bridge is deployed to VPS, and a Mac-played `你好小智` wake test validates that the board receives and writes 3200 PCM bytes to VB6824 playback.
- OpenClaw on the VPS is installed and exposes four agents, including `huntmind`. Root CLI and the Bridge service path can run `openclaw agent --agent huntmind --message ... --session-key ...`; a two-turn smoke test confirms session continuity.
- The deployed Bridge service maps `target=huntmind` through a restricted local wrapper, so the public device host still exposes only the Bridge, not OpenClaw itself.
- Firmware serial `:target <agent>` updates only `xob.default_target` and reboots. The AP provisioning page treats `default_target` as a free-form route name, not as a WiFi choice.
- The flashed board has `default_target=huntmind`. The earlier fake-ASR transport loop was superseded by the Bailian ASR validation below.
- `huntmind` is the current OpenClaw agent route. Hermes is a separate Bridge
  route, and the remote Hermes runtime is configured as the `xiaoyuan` / `小元`
  persona.
- Hermes/Lobster should be integrated as Agent adapters behind the Bridge, not as board-side runtimes or TTS providers. The XiaoZhi-like server role is the Bridge voice gateway: device protocol, ASR/VAD, Agent routing, TTS, playback state, interrupt, and audit.
- LAN Hermes first integration path is the host-local Hermes Agent CLI on
  `example@192.0.2.10` (example): `/usr/local/bin/hermes -z <prompt>` over SSH with a
  dedicated key. Docker dashboard/gateway discovery is not the Bridge adapter
  path for this phase.
- Hermes native ASR/TTS does not replace the Bridge voice gateway. If it proves
  useful, add it later as configurable `AsrProvider` / `TtsProvider`
  implementations after the streaming, interrupt, and audio-format contracts
  are confirmed.
- ASR now has an opt-in OpenAI provider path that wraps HTTP PCM16 as WAV and VB6824 WebSocket Opus frames as Ogg Opus before transcription. It is not enabled on the VPS because `OPENAI_API_KEY` is not configured.
- ASR has Alibaba Cloud Bailian/DashScope provider paths. Fun-ASR-Flash was the
  first bring-up provider; the current VPS voice-chain path uses
  `XOB_ASR_PROVIDER=bailian_paraformer_realtime` with
  `XOB_BAILIAN_PARA_ASR_MODEL=paraformer-realtime-v2`. The API key is stored
  only in `/etc/xob-bridge/bailian-asr.env`, not in Git.
- VPS ASR now pins `XOB_BAILIAN_ASR_WS_URL` to the DashScope WebSocket endpoint
  so Paraformer realtime uses the user's speech-model quota context. Keep this
  separate from `XOB_BAILIAN_BASE_URL`, which may point at a Bailian workspace
  host for other providers.
- Board serial `:vb-talk` previously validated the real chain through VPS with raw PCM playback: VB6824 Opus input frames, Bailian ASR transcription, OpenClaw `huntmind`, MiniMax/Bailian TTS, server-side PCM framing, and VB6824 playback. The 2026-07-07 route now replaces the server-to-board TTS leg with OPUS packets to match the original XiaoZhi network audio shape.
- Bridge can still strip WAV headers for compatibility, but the selected voice path is now Bridge-side WAV/PCM-to-OPUS packetization plus firmware-side OPUS decode before VB6824 PCM output. In voice mode, the OpenClaw adapter asks the agent to generate a short spoken answer at the source; the Bridge also chunks returned text into XiaoZhi-style `sentence_start` segments before TTS instead of using hard truncation as the primary strategy.
- Audio playback follows the original XiaoZhi/DOIT pipeline shape: WebSocket receive enqueues returned audio, while a dedicated playback path feeds VB6824. Do not pace speaker output directly from WebSocket binary frame boundaries.
- Current stable board test passed no-interrupt continuous dialogue after the
  LISTENING display fix and 1.5-second speech-end tail. Firmware pads the start
  of each TTS playback session with 40 ms of silence to avoid clipping the first
  syllable; this is a playback onset smoothing tweak, not a protocol change.
- Product target is the original XiaoZhi voice experience: short press starts
  `mode:auto`, listening ends by automatic VAD/endpointer, short press while
  listening cancels back to idle, speaking can be interrupted into a new listening turn,
  and responses are streamed/low-latency. Non-streaming transcription and
  per-turn WebSocket sessions are acceptable only as bring-up fallbacks, not the
  final interaction model.
- Local-only upstream XiaoZhi reference clones are archived under
  `.local-references/xiaozhi/` and ignored by Git. The source check confirms the
  missing UX gap: in upstream auto/realtime mode, `tts stop` returns to
  listening after playback completion; manual mode returns to idle.
- Current firmware has validated XiaoZhi-style no-interrupt continuous dialogue
  on the flashed board: stock `你好小智` wake, first spoken turn, TTS playback,
  automatic re-entry to listening without a middle-button press, second spoken
  turn on the same WebSocket session, and silent idle timeout when no third
  utterance follows.
- 2026-07-08 physical middle-button interrupt testing showed the old restart
  path could surface board-side ERROR after interrupt. The current target is the
  XiaoZhi-style same-WebSocket `abort` path. Follow-up real-board testing passed
  the basic middle-button interrupt path; the remaining work is polish and full
  long-lived audio-channel semantics.
- 2026-07-08 14:11-14:13 board log review after the latest toggle-alignment
  build showed normal communication again: one `/device/ws` session opened,
  multiple ASR -> HuntMind -> TTS turns completed, the WebSocket closed normally,
  and no reboot/`POST /device/hello` appeared after the 14:09 reconnect.
- Remaining stable-version gaps: WiFi/Bridge provisioning must stay easy to
  change per environment; human-spoken `你好小智` wake reliability and Xiaoyuan
  voice-pack OTA remain unresolved; and CLI latency is still the dominant
  thinking delay for synchronous agent targets. This branch validates the
  Hermes connection gap via the LAN host-local CLI route, while keeping board
  target selection configurable.
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

## Phase 3 - Hermes Adapter

Goal: connect Bridge to the LAN-local Hermes Agent CLI while keeping Bridge a
voice/session transport. Hermes owns its model, tools, skills, approvals, and
desktop capabilities.

Tasks:

- [x] Confirm LAN Hermes host-local CLI entry over SSH.
- [x] Confirm the remote Hermes persona/agent is `xiaoyuan` / `小元`; `huntmind`
  remains OpenClaw-only.
- [x] Implement `hermes-cli` target adapter.
- [x] Normalize basic text status, summary, and artifact metadata.
- [x] Add one smoke test or fixture test.
- [x] Validate Bridge `/command` with `target:"hermes"` against the LAN CLI.
- [x] Keep Hermes native ASR/TTS as a future provider option, not a reason to
  replace the Bridge gateway in this branch.
- [x] Temporarily switch the board to `:target hermes`, confirm the currently
  configured Bridge returns HTTP 400 for that target, then restore
  `:target huntmind`.
- [x] Deploy this branch's Bridge on the private Hermes host (example SSH target:
  `example@192.0.2.10`) as
  `xob-bridge-hermes.service`, listening on `0.0.0.0:8788`, with
  `target=hermes` routed to the host-local `/usr/local/bin/hermes`.
- [x] Point the board at its configured Hermes Bridge (example:
  `http://192.0.2.10:8788`) and set
  `xob.default_target=hermes` for full board-to-Hermes testing.
- [x] Increase firmware `/device/command` timeout to 30 seconds for the current
  synchronous Hermes CLI bring-up path.
- [x] Retire the bring-up-only `--safe-mode`, `--toolsets`, and `--skills`
  injection from the Bridge adapter. The host-local Hermes launcher now owns
  intent/session Skill preload and bounded tool selection as part of the
  Xiaoyuan harness.
- [x] Configure the Hermes-branch Bridge deployment with the same real
  ASR/TTS/OPUS voice-chain environment as the validated OpenClaw Bridge.
- [ ] Add Lobster adapter using the same `AgentRequest` / `AgentResponse` contract after its API/CLI shape is confirmed.
- [x] Record the synchronous Hermes CLI route as the first-round connectivity
  baseline, not the final product-demo architecture.

Acceptance:

- Hermes can be selected by `target:"hermes"` using the same `/command` API.
- See `docs/PHASE3_HERMES_LAN_CLI.md` for this branch's Hermes LAN task list
  and validation evidence.

## Phase 3B - Logical Hermes Session Baseline

Goal: validate Xiaoyuan's multi-turn semantics over Hermes' supported CLI
session surface while keeping Bridge as the board-facing voice gateway.

Status: logical Hermes session reuse is validated on the CLI compatibility
path. Per-turn process removal, supported event transport, and cancellation move
to Phase 3C; the existing Hermes Skills and product chains remain unchanged.

Tasks:

- [x] Verify the supported Hermes persistent-session surface:
  `hermes chat --quiet --resume <session_id>` / `--continue <title>`.
- [x] Map Bridge sessions to reusable Hermes sessions by creating the first
  Hermes chat turn, reading `session_id: ...`, then renaming the Hermes session
  to the Bridge session key for later `--continue`.
- [x] Keep product routing out of the Bridge adapter; forward the prompt and
  session key to a host-local Hermes harness that preloads the required Skill
  with official `--skills` support and fails closed when it is missing.
- [x] Provide the host-local Hermes process with the active graphical-session
  environment so Hermes Computer Use can operate the visible desktop.
- [x] Validate the corrected HiBrush product harness on the real Bridge
  session: MiniMax-M3 interprets subject/template/prompt semantics, while the
  runner performs actual HiBrush operations. Prompt mode prepared an LLM-written
  portrait prompt in 25.802 seconds; matching-template mode searched `人像`
  then `女性` and selected `/templates/prompt-523` in 46.668 seconds; a later
  different-template request selected `/templates/prompt-1027` in 36.386
  seconds. All three board-equivalent voice turns stayed under 90 seconds and
  submitted no generation task.
- [x] Preserve the current Bridge ASR/TTS/OPUS route during logical session
  validation.
- Product-grade process persistence, events, cancellation, semantic Skill
  selection, and latency acceptance are tracked only in Phase 3C.

Acceptance:

- Bridge can reuse a stable Hermes session key across related turns.
- Xiaoyuan resolves a follow-up such as selecting a different template from the
  prior turn without submitting a real generation task.
- This phase does not claim process persistence or a supported event stream.
- See `docs/PHASE3_HERMES_LAN_CLI.md` for the decision and detailed boundary.

## Phase 3C - Persistent Hermes Agent Bridge

Goal: remove per-turn Hermes CLI startup cost while preserving the current
voice route and every existing server-side Skill, company/product introduction,
media asset, harness/launcher chain, runner, persona, and approval policy.

Execution priority (updated 2026-10-18 after comparison with
[`78/xiaozhi-esp32@e0d2390`](https://github.com/78/xiaozhi-esp32/tree/e0d2390a1ca415473508bbc0c66eaf1d8dc04e37)):

- [ ] **P0:** Gate 0 read-only baseline: hashes and effective calling contracts
  for existing Skills, company introductions, HiBrush harness/launcher, runners,
  and configuration, plus segmented cold/warm latency. No business-chain edits.
- [ ] **P1.1:** Separate socket reception from background ASR/Agent/TTS work;
  same-WebSocket abort must be handled while the Agent is still thinking.
- [ ] **P1.2:** Reuse existing playback buffers; add bounded queues/backpressure,
  playback generation guards, and drained events. Verify ten consecutive aborts
  produce no stale-turn playback or growing queues; normal auto listening waits
  for actual playback completion, including in-flight decode/output.
- [ ] **P1.3:** Complete persistent Hermes Gates 1–3 and wire cancellation into
  P1.1/P1.2. Ten warm turns must reuse the runtime with identical harness routing,
  Skill preload, toolsets, session affinity, and execution contracts.
- [ ] **P2.1:** Stream speakable Agent segments through TTS and incremental Opus;
  first audio must precede full-answer completion. Evaluate live ASR afterwards;
  partial transcripts must not execute business tasks.
- [ ] **P2.2 — before public-network enablement:** Add board WSS/TLS and connection
  recovery, verify certificate validation, reject invalid certificates, and never
  fall back to plaintext. Current board WebSocket accepts only HTTP Bridge URLs;
  nginx TLS alone does not complete this path.
- **P3 — deferred:** Reassess upstream firmware reuse/device MCP on a board change
  or concrete device-control need. No current MQTT+UDP, AEC/full-duplex, broad
  board-porting, or whole-firmware replacement work. Upstream IDF 6.x cannot be
  mixed directly into this project's documented IDF 5.3.5 build baseline.

Priority order controls implementation; Gates 0–5 below retain their interface,
business-regression, and cutover checks. Existing server harness phrases/aliases,
routes, Skill preload, toolsets, session bindings, and runners stay unchanged.
If a persistent interface requires changing that chain, reject that integration
option and report the incompatibility while retaining the current path.
Protected configuration means existing Hermes business-chain configuration;
new Bridge/transport configuration must be kept separate without overwriting it.

Tasks:

- [ ] Record read-only before/after inventories, hashes, and calling contracts
  for protected Hermes Skills, product-introduction assets, existing harness/
  launcher, runners, and configuration; stop if they change.
- [ ] Benchmark the current CLI path by separating process startup, first Agent
  event, final result, and first TTS audio latency.
- [ ] Confirm a supported long-lived Hermes interface and its license/stability
  boundary; do not scrape interactive CLI output.
- [ ] Preserve the existing host-local harness unchanged as the required routing
  gate for company-introduction and HiBrush intents; adapt the new persistent
  interface around its current Skill preload, toolsets, and session affinity.
- [ ] Add a long-lived broker/profile-worker/Agent-session path and prove ten
  warm turns reuse one broker instead of spawning `hermes chat` per turn.
- [ ] Add a configurable `hermes-bridge` adapter while preserving `hermes-cli`
  as an explicit fallback.
- [ ] Keep product rules out of firmware and the Bridge adapter. The host-local
  harness guarantees protected Skill selection; Hermes/LLM interprets the
  concrete task and parameters inside that loaded Skill contract, and the
  existing Skill/runner safety contract still owns execution.
- [ ] Forward started, speakable, tool progress, approval, clarify, final,
  error, and cancelled events through Bridge with stable turn/generation IDs;
  discard stale output after abort or a newer turn starts.
- [ ] Make board abort stop TTS and cancel only the active Hermes run while
  clearing that turn's queued output and retaining completed conversation context.
- [ ] Validate canonical phrases, approved harness aliases, three-turn route
  affinity, HiBrush prepare-only, and an approved company-introduction read/play
  path without modifying their server-side implementations.
- [ ] Compare protected-file hashes after validation, then cut over by config
  only if the warm path beats the CLI baseline and rollback remains available.

Acceptance:

- Warm turns reuse a persistent Hermes Agent runtime and do not spawn a new CLI
  process per request.
- The host-local harness deterministically selects protected business Skills;
  LLM semantic understanding operates inside the selected Skill contract, while
  ambiguous or side-effecting tasks still require clarification or approval.
- Existing Hermes Skills, company/product introduction assets, harness/launcher,
  runners, and configuration are byte-for-byte unchanged across the upgrade,
  with identical business routing and calling contracts.
- CLI fallback, progress, interruption, session continuity, and no-duplicate
  side-effect behavior are all verified.
- See `docs/PHASE3C_HERMES_PERSISTENT_BRIDGE.md` for the upgraded taskbook,
  gates, metrics, and explicit non-goals.

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
- [x] Validate audible returned-audio playback with non-silent TTS.
- [x] Configure Bridge service to route `target=huntmind` through a restricted OpenClaw CLI wrapper.
- [x] Switch flashed board `default_target` from `fake` to `huntmind`.
- [x] Validate board `:vb-talk` through VPS Bridge to OpenClaw `huntmind` and returned WAV playback write.
- [x] Add opt-in OpenAI ASR provider path for PCM16/WAV and VB6824 Opus-frame input.
- [x] Add opt-in Alibaba Cloud Bailian Fun-ASR-Flash provider path for the first real ASR bring-up.
- [x] Configure VPS Bridge with Bailian Paraformer realtime and validate real spoken Chinese from the board.
- [x] Chunk returned MiniMax WAV into firmware-friendly WebSocket frames and validate VB6824 PCM playback with real agent output.
- [x] Add VB6824 playback queue so WebSocket receive is decoupled from fixed-cadence UART playback.
- [x] Pad playback session start with 40 ms silence to reduce first-syllable clipping without changing the streaming protocol.
- [x] Add voice-mode OpenClaw prompt shaping so spoken replies are short plain Chinese instead of long Markdown sent to TTS.
- [x] Add XiaoZhi-style spoken text segmentation before TTS and validate OPUS segment playback through VPS.
- [x] Add VB6824 Opus-frame VAD/endpointer for button and wake listening.
- [ ] Add Opus decode plus VAD/endpointer on the Bridge, matching XiaoZhi auto-stop behavior.
- [x] Configure a streaming-capable ASR provider and validate real spoken Chinese from the board.
- [x] Add XiaoZhi-style no-interrupt continuous dialogue: after TTS playback
  completes in auto/realtime mode, re-enter listening; if no speech follows,
  time out back to idle without submitting an empty ASR turn.
- [x] Validate no-interrupt continuous dialogue on the real board with two
  consecutive user utterances and no middle-button press between them.
- [ ] Replace the temporary per-turn button state machine with XiaoZhi-style
  long-lived audio-channel semantics: idle -> listening, listening -> cancel/close,
  thinking/speaking -> abort and re-enter listening.
- [ ] Validate middle-button listening cancel-to-idle and speaking interrupt on
  the real board after the toggle-semantics change.
- [x] Review VPS logs for the latest real-board run after the toggle-semantics
  change and confirm no server-side communication error or reboot loop.
- [x] Re-test physical middle-button paths after the current stable voice-loop
  snapshot.
- [ ] Display recognized text and final answer summary.
- [ ] Drive eye states from audio lifecycle: listening, thinking, speaking, error.

Acceptance:

```text
press button
  -> speak command
  -> Bridge transcribes
  -> OpenClaw/Hermes responds
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
- Public internet does not expose OpenClaw/Hermes/Zebra directly.

## Next Task

Start Phase 3C P0 / Gate 0: record read-only protected-file hashes, current
harness/launcher calling contracts, and a segmented CLI latency baseline. Next
implement P1.1 socket/turn separation and P1.2 generation/drained-queue handling,
then complete P1.3 persistent Hermes integration against that cancellation path.
Existing server Skills, company introductions, and HiBrush harness/runner chains
stay unchanged throughout. Gate 0 does not deploy, restart, or run real product
side effects; keep the current CLI path until separate cutover checks pass.

Do not store WiFi passwords, device tokens, VPS connection strings, flash backups, or raw device identifiers in Git.
