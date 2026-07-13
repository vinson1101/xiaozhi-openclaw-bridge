# Phase 3 Hermes LAN CLI Adapter

This document records the completed first-round connection from the Bridge to
the LAN-local Hermes Agent CLI. It does not change the existing OpenClaw route.

Primary work in this branch should stay in the Bridge adapter and Hermes CLI
integration path. Firmware work is limited to configurable target selection and
the command timeout needed for the synchronous Hermes CLI bring-up route, not
embedding a Hermes host, voice, or agent constant.

## Status

The synchronous CLI route is complete as a connectivity baseline: a board turn
can reach the `xiaoyuan` / `小元` persona and return through the existing Bridge
ASR/TTS/OPUS path. It is not the final product-demo architecture because a
single `hermes chat` invocation cannot preserve a live visitor conversation or
report progress while a Skill is running.

Phase 3B now keeps Bridge as the device voice gateway and replaces the
per-turn Hermes process with Bridge-managed Hermes chat sessions. The live
Hermes CLI supports non-interactive `chat --quiet`, `--resume <session_id>`,
and `--continue <title>`. It does not expose a stable progress-event stream in
the CLI help, so progress/approval event forwarding remains a separate follow-up
instead of being faked by scraping interactive output.

## Goal

Make `target:"hermes"` work through the same Bridge `AgentRequest` /
`AgentResponse` contract:

```text
Bridge /command or voice route
  -> target=hermes
  -> SSH to ubuntu@192.168.110.30
  -> /usr/local/bin/hermes -z <prompt>
  -> remote Hermes persona/agent: xiaoyuan
  -> normalized Bridge response
```

## Configuration Contract

Runtime choices must stay configurable. Do not hard-code WiFi, Bridge hosts,
OpenClaw routes, Hermes hosts, TTS voices, voice packs, or agent objects into
firmware or adapter code.

Current configuration surfaces:

- Board WiFi / Bridge URL / device token / default target: `xob` NVS via serial
  or AP provisioning.
- Agent target aliases: `XOB_AGENT_TARGETS`.
- OpenClaw route: `XOB_OPENCLAW_*`.
- Hermes route: `XOB_HERMES_*`.
- Hermes persona/agent: configured inside the remote Hermes runtime as
  `xiaoyuan` / `小元`; `target=hermes` is only the Bridge route name.
- TTS provider/model/voice: existing TTS environment variables such as
  `XOB_TTS_PROVIDER`, `XOB_BAILIAN_TTS_MODEL`, and `XOB_BAILIAN_TTS_VOICE`.

The local LAN Hermes route is:

```bash
export XOB_AGENT_TARGETS='hermes=hermes-cli:XOB_HERMES'
export XOB_HERMES_SSH_TARGET='ubuntu@192.168.110.30'
export XOB_HERMES_SSH_KEY="$HOME/.ssh/xob_hermes_lan_ed25519"
export XOB_HERMES_CLI_BIN='/usr/local/bin/hermes'
export XOB_HERMES_ENABLE_COMMANDS=1
export XOB_HERMES_SAFE_MODE=1
export XOB_HERMES_TOOLSETS=safe
```

The password used to install the SSH key is not part of the runtime config and
must not be stored in Git.

`huntmind` is the existing OpenClaw agent route. It is not a Hermes agent.

## Non-goals

- Do not replace or break the existing OpenClaw `huntmind` route.
- Do not depend on the Docker dashboard/gateway path for this phase.
- Do not move Hermes, OpenClaw, Zebra, or TTS providers into firmware.
- Fastest current voice path: keep the existing Bridge voice gateway and its
  ASR/TTS/OPUS chain; this branch only adds Hermes as another Agent target.
- Do not let Hermes bypass the Bridge voice gateway just because it has native
  ASR/TTS. Hermes native TTS may change the provider choice, but only after it
  is designed and validated as a configurable Bridge `TtsProvider` whose output
  still goes through Bridge segmentation, OPUS packetization, playback state,
  interrupt, and fallback handling.
- This first round does not add a persistent Hermes session or progress-event
  adapter. That work belongs to Phase 3B and must use a supported Hermes
  session/event surface rather than parsing interactive CLI output.

## Tasks

- [x] Create a dedicated branch for LAN Hermes work.
- [x] Install a dedicated SSH key for `ubuntu@192.168.110.30`.
- [x] Verify host-local `/usr/local/bin/hermes -z` over SSH.
- [x] Verify the remote Hermes runtime answers as `xiaoyuan` / `小元`.
- [x] Add `hermes-cli` adapter kind.
- [x] Configure local ignored `.env` for `target=hermes`.
- [x] Add smoke coverage for `hermes=hermes-cli:XOB_HERMES`.
- [x] Validate Bridge `/command` with `target:"hermes"`.
- [x] Decide Hermes native ASR/TTS does not change the current Bridge gateway
  role; keep it as a later provider option.
- [ ] Evaluate Hermes native TTS as a Bridge `TtsProvider` only after its
  CLI/API, audio format, streaming behavior, latency, and fallback semantics
  are confirmed.
- [x] Temporarily switch board `xob.default_target` to `hermes` with
  `:target hermes`, verify the currently configured Bridge returns 400 for that
  target, then restore `:target huntmind`.
- [x] Deploy this branch's Bridge on `ubuntu@192.168.110.30` as
  `xob-bridge-hermes.service`, listening on `0.0.0.0:8788`, with
  `target=hermes` routed to the host-local `/usr/local/bin/hermes`.
- [x] Point the board at `http://192.168.110.30:8788` and set
  `xob.default_target=hermes` for full board-to-Hermes testing.
- [x] Increase the firmware `/device/command` HTTP timeout to 30 seconds so the
  board can wait for the current synchronous Hermes CLI response.
- [x] Prevent Hermes CLI from recursively calling the Bridge during voice
  bring-up by running it with `--safe-mode --toolsets safe`.
- [x] Configure `xob-bridge-hermes.service` with the same real Bridge
  ASR/TTS/OPUS voice-chain environment as the validated OpenClaw deployment.
- [ ] Later: replace or supplement `hermes-cli` with a richer Hermes API only
  after the API contract is confirmed.

## Validation

Current validated local command:

```text
ssh ubuntu@192.168.110.30 /usr/local/bin/hermes -z "只回复两个字：收到"
  -> 收到

ssh ubuntu@192.168.110.30 /usr/local/bin/hermes -z "只回复你的名字，不要解释。"
  -> 小元
```

Current validated Bridge route:

```text
POST /command {"target":"hermes","text":"只回复两个字：收到"}
  -> status=done
  -> text=收到

curl http://192.168.110.30:8788/command {"target":"hermes","text":"只回复两个字：收到"}
  -> status=done
  -> text=收到
```

Current real-board target-only check:

```text
:target hermes
serial text command "只回复两个字：收到"
  -> current configured Bridge returned HTTP 400
:target huntmind
  -> restored; /device/hello returned 200
```

Current real-board Hermes check:

```text
board NVS:
  xob.bridge_url -> http://192.168.110.30:8788
  xob.default_target -> hermes

boot:
  WiFi connected via fallback
  default_target=hermes
  POST /device/hello -> 200

serial text command "只回复两个字：收到"
  -> board: device command status=200
  -> Bridge: POST /device/command -> 200
  -> Bridge adapter: target=hermes source=device returncode=0 elapsed_ms~=11200
```

Current log finding and mitigation:

```text
Symptom:
  voice text "检查链路" caused Hermes CLI to run shell/curl diagnostics.
  The diagnostic called POST /command target=hermes on the same Bridge,
  creating Bridge -> Hermes -> Bridge -> Hermes recursion.

Mitigation:
  xob-bridge-hermes.service sets:
    XOB_HERMES_SAFE_MODE=1
    XOB_HERMES_TOOLSETS=safe

Validation:
  POST /command target=hermes text="检查链路" context.mode=voice
    -> single Hermes CLI call
    -> no nested curl or Hermes process
    -> elapsed_ms~=6800

  board serial text "检查链路，确认系统状态"
    -> board: device command status=200
    -> Bridge: target=hermes source=device elapsed_ms~=10700
```

Current deployment source and voice-chain check:

```text
192.168.110.30:/home/ubuntu/xiaozhi-openclaw-bridge
  -> git checkout origin/codex/hermes-lan-ssh
  -> commit ba42566

xob-bridge-hermes.service
  -> XOB_ASR_PROVIDER=bailian_paraformer_realtime
  -> XOB_TTS_PROVIDER=bailian
  -> XOB_BAILIAN_TTS_MODEL=cosyvoice-v3-flash
  -> XOB_BAILIAN_TTS_VOICE=longyan_v3
  -> XOB_WS_TTS_AUDIO_CODEC=opus

WebSocket tts_debug
  -> summary=bailian tts 8 chars model=cosyvoice-v3-flash voice=longyan_v3
  -> tts_audio_frames=36
  -> tts_audio_bytes=4320
```

## Phase 3B - Persistent Hermes Demo Session

### Decision

Keep Bridge as the board-facing voice gateway. It continues to own device
authentication, ASR, TTS, OPUS packetization, playback state, interrupt, and
audit. Hermes becomes one persistent conversation per visitor, rather than one
CLI process per spoken turn. Hermes native microphone/speaker mode remains a
host-debug feature, not the board integration path.

```text
XiaoZhi board
  <-> Bridge voice session
  <-> persistent Hermes session for xiaoyuan
```

### Scope

1. Use Hermes' supported persistent chat interface. First turn creates a Hermes
   session and returns `session_id: ...`; Bridge renames that session to its
   stable Bridge session key. Later turns use `--continue <Bridge session key>`.
2. Keep the mapping in Bridge-owned identifiers and backend artifacts:
   `device_session_id -> bridge_session_id -> hermes_session_key`.
3. Do not emulate progress by scraping interactive CLI output. Add event
   forwarding only when Hermes exposes a supported event/API surface.
4. Translate only the events needed for the demo: started, speakable text,
   tool progress, final result, approval, error, and cancellation.
5. Keep the existing Bridge ASR/TTS/OPUS providers for the first Phase 3B
   validation. Hermes-native STT/TTS is not required.
6. Route board `abort` to both TTS stop and the active Hermes run, while
   retaining completed conversation context for the next turn.

### Acceptance

- A visitor can ask three related questions, including a reference such as
  "刚才那个产品", and Xiaoyuan answers from the same Hermes conversation.
- During a product-demo Skill, the board announces work has started and at
  least one progress or completion event before returning to listening.
- A board interrupt stops speech, cancels the active task, and a new spoken
  turn remains usable without creating a duplicate visitor session.
