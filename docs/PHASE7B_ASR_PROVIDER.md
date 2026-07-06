# Phase 7B ASR Provider Adapter

This slice adds the server-side ASR provider boundary and the first real
bring-up provider.

## Scope

- `AsrRequest`
- `AsrResponse`
- `asr_provider_for(...)`
- `FakeAsrProvider`
- optional `OpenAIAsrProvider`
- optional `BailianFunAsrFlashProvider`

The fake provider is only for local wiring checks. It returns
`XOB_FAKE_ASR_TEXT` when set, otherwise `你好，检查链路`.

`OpenAIAsrProvider` is available when `XOB_ASR_PROVIDER=openai` is set. It is
not enabled by default. It requires `OPENAI_API_KEY` and defaults to
`gpt-4o-mini-transcribe`, overrideable with `XOB_OPENAI_ASR_MODEL`.

`BailianFunAsrFlashProvider` is available when
`XOB_ASR_PROVIDER=bailian_fun_flash` is set. It is not enabled by default. It
uses Alibaba Cloud Model Studio / DashScope `fun-asr-flash-2026-06-15` through
the HTTP multimodal generation endpoint, with `XOB_BAILIAN_API_KEY` or
`DASHSCOPE_API_KEY`, `XOB_BAILIAN_BASE_URL`, and optional
`XOB_BAILIAN_ASR_MODEL`.

Input handling:

- HTTP `/device/audio` is treated as PCM16 and wrapped as WAV before upload.
- WebSocket VB6824 audio is treated as raw Opus frames and wrapped as Ogg Opus
  before upload.
- If no API key is configured, the provider returns an ASR error instead of
  making any network call.

## Boundary

Fake mode does not upload real audio to any external provider. OpenAI mode is
explicit opt-in through environment variables and should only be enabled after
cost and privacy expectations are confirmed. Bailian Fun-ASR-Flash is also
explicit opt-in. It is the first real ASR bring-up provider because the user has
free Alibaba Cloud speech-model quota, but it is still a non-real-time
recorded-audio path. The final XiaoZhi-like experience still needs streaming
ASR plus server-side VAD/endpointer.

Alibaba Cloud TTS models are also available in the same account and can become a
second dynamic TTS provider later. The current shortest path keeps the already
deployed MiniMax TTS while replacing fake ASR first.

## VPS Bring-Up

The VPS Bridge now runs with `XOB_ASR_PROVIDER=bailian_fun_flash`. The Bailian
API key is stored only in `/etc/xob-bridge/bailian-asr.env`, not in the repo.

Validation:

```text
public sample WAV -> Hello World，这里是阿里巴巴语音实验室。
board :vb-talk -> ASR text 测试成功。 -> OpenClaw huntmind
```

## Validation

```bash
python3 scripts/check_asr_provider.py
```

Expected output:

```text
check_asr_provider ok
```
