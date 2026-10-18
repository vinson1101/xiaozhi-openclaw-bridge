# Phase 3 Hermes LAN CLI Adapter

This document records the completed first-round connection from the Bridge to
the LAN-local Hermes Agent CLI. It does not change the existing OpenClaw route.

This file remains the historical CLI baseline and validation record. The current
upgrade scope and implementation gates are defined in
`docs/PHASE3C_HERMES_PERSISTENT_BRIDGE.md`.

The historical scope recorded here stayed in the Bridge adapter and Hermes CLI
integration path. New work belongs to Phase 3C. Firmware remains limited to
configurable target selection and the voice-gateway contract, not embedding a
Hermes host, voice, or agent constant.

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
  -> SSH to example@192.0.2.10 (example target)
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
- Hermes model, approvals, and Computer Use policy: configured by Hermes. The
  Bridge adapter remains product-agnostic; `XOB_HERMES_CLI_BIN` points to the
  host-local Xiaoyuan harness, which preloads the required Skill and its bounded
  toolsets from the current intent/session before invoking Hermes.
- TTS provider/model/voice: existing TTS environment variables such as
  `XOB_TTS_PROVIDER`, `XOB_BAILIAN_TTS_MODEL`, and `XOB_BAILIAN_TTS_VOICE`.

The following LAN Hermes values are examples; they are not the live endpoint:

```bash
export XOB_AGENT_TARGETS='hermes=hermes-cli:XOB_HERMES'
export XOB_HERMES_SSH_TARGET='example@192.0.2.10'
export XOB_HERMES_SSH_KEY='/absolute/path/to/hermes/key'
export XOB_HERMES_CLI_BIN='/home/example/xiaozhi-openclaw-bridge/scripts/hermes_cli_with_desktop_env.sh'
export XOB_HERMES_ENABLE_COMMANDS=1
```

Real local connection settings stay in the ignored `.env`. The ignored private
record `.local/hermes-connection.env` records the actual host, SSH aliases, key
path, remote project/harness paths, and Bridge URL. Neither file contains an SSH
password or private-key material. Preserve existing values when updating this repo.

The Bridge reads process environment variables; it does not load `.env` itself.
For a local shell run, export the existing local file before starting Bridge:

```bash
set -a
. ./.env
set +a
PYTHONPATH=src python3 -m xiaozhi_openclaw_bridge.server --host 127.0.0.1 --port 8788 --db data/bridge.sqlite3
```

For a new environment, copy `.env.example` to `.env` and fill in its values.
For Bridge running on the Hermes host, use
`deploy/hermes/xob-bridge-hermes.env.example` as the template for the ignored
`deploy/hermes/xob-bridge-hermes.env`; its `SSH_TARGET=local` invokes the same
host-local harness. A Mac uses SSH to reach that host. These are distinct execution
locations, not a change to the Skill/company-introduction/HiBrush harness chain.

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
- [x] Install a dedicated SSH key for the private Hermes host (example:
  `example@192.0.2.10`).
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
- [x] Deploy this branch's Bridge on the private Hermes host (example:
  `example@192.0.2.10`) as
  `xob-bridge-hermes.service`, listening on `0.0.0.0:8788`, with
  `target=hermes` routed to the host-local `/usr/local/bin/hermes`.
- [x] Point the board at its configured Hermes Bridge (example:
  `http://192.0.2.10:8788`) and set
  `xob.default_target=hermes` for full board-to-Hermes testing.
- [x] Increase the firmware `/device/command` HTTP timeout to 30 seconds so the
  board can wait for the current synchronous Hermes CLI response.
- [x] Retire the bring-up-only `--safe-mode --toolsets safe` mitigation.
  The Bridge adapter no longer chooses Hermes capabilities; the host-local
  launcher enforces Xiaoyuan's Skill contract and must not call the Bridge back
  as a tool.
- [x] Configure `xob-bridge-hermes.service` with the same real Bridge
  ASR/TTS/OPUS voice-chain environment as the validated OpenClaw deployment.
- [ ] Later: replace or supplement `hermes-cli` with a richer Hermes API only
  after the API contract is confirmed.

## Validation

Endpoints and deployment paths in the historical records below have been replaced
with example values. Results are retained as historical evidence; use the ignored
local configuration/private record for the actual target.

Current validated local command:

```text
ssh example@192.0.2.10 /usr/local/bin/hermes -z "只回复两个字：收到"
  -> 收到

ssh example@192.0.2.10 /usr/local/bin/hermes -z "只回复你的名字，不要解释。"
  -> 小元
```

Current validated Bridge route:

```text
POST /command {"target":"hermes","text":"只回复两个字：收到"}
  -> status=done
  -> text=收到

curl http://192.0.2.10:8788/command {"target":"hermes","text":"只回复两个字：收到"}
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
  xob.bridge_url -> http://192.0.2.10:8788
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

Historical bring-up finding and current boundary:

```text
Symptom:
  voice text "检查链路" caused Hermes CLI to run shell/curl diagnostics.
  The diagnostic called POST /command target=hermes on the same Bridge,
  creating Bridge -> Hermes -> Bridge -> Hermes recursion.

Current mitigation:
  The Bridge adapter forwards only the turn and session key. The configured
  host-local Hermes launcher routes the intent/session, preloads the installed
  Skill through official `--skills`, supplies its bounded toolsets, and fails
  closed if that Skill is missing. Xiaoyuan must not call the board-facing
  Bridge endpoint from inside a Hermes turn.

Validation:
  adapter + launcher smoke
    -> adapter emits `hermes chat --quiet --source xiaozhi-bridge --continue ... --query ...`
    -> launcher appends the routed `--skills` and `--toolsets`
    -> missing required Skill exits 78 before Hermes runs
```

Current HiBrush harness acceptance (2026-07-15):

```text
Bridge session 26ec71ad-5076-47a2-a3fe-7242b9a059b7
  "提示词你自己写", do not generate
    -> elapsed 25.802 s
    -> MiniMax-M3 API calls: 2
    -> runner: prepare --mode prompt
    -> LLM-written portrait prompt length: 59
    -> generationSubmitted:false

  "选择匹配的现成模板", do not generate
    -> elapsed 46.668 s
    -> LLM searches `人像`, then refines to `女性`
    -> selected /templates/prompt-523
    -> generationSubmitted:false

  "换一个不同的现成模板", do not generate
    -> elapsed 36.386 s
    -> selected /templates/prompt-1027 instead of the prior template
    -> generationSubmitted:false

Boundary:
  Harness owns protected Skill selection and session route affinity.
  LLM owns user semantics and HiBrush search vocabulary.
  Skill/runner owns the website operations and one-submit safety contract.
  Fixed prompt-590 remains a prepare-only health-check fixture.
```

Current deployment source and voice-chain check:

```text
192.0.2.10:/home/example/xiaozhi-openclaw-bridge
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
