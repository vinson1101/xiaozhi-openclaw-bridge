# Phase 7C TTS Provider Adapter

This slice adds the server-side TTS provider boundary, fixed-prompt cache keys,
and an opt-in MiniMax dynamic-answer provider.

## Scope

- `TtsRequest`
- `TtsResponse`
- `tts_provider_for(...)`
- `FakeTtsProvider`
- `MiniMaxTtsProvider`
- `synthesize_fixed_prompt(...)`

The fake provider returns a small valid non-silent WAV tone. It is only a
wiring placeholder for tests and device playback plumbing.

`MiniMaxTtsProvider` is selected with:

```bash
export XOB_TTS_PROVIDER=minimax
export XOB_MINIMAX_API_KEY='<same key used by OpenClaw MiniMax provider>'
```

Optional tuning variables:

```bash
export XOB_MINIMAX_TTS_MODEL='speech-2.8-turbo'
export XOB_MINIMAX_TTS_VOICE='Chinese (Mandarin)_Gentleman'
export XOB_MINIMAX_TTS_SAMPLE_RATE=16000
export XOB_MINIMAX_TTS_FORMAT=wav
```

MiniMax provider output is 16 kHz mono WAV in the old fallback path. For the
current voice-experience path, provider WAV/PCM must be converted on the VPS to
XiaoZhi-style OPUS packets before it reaches the board. Firmware WAV/PCM support
is kept only for debug compatibility.

## Fixed Prompt Cache

`synthesize_fixed_prompt(prompt_id)` reads prompt text and `cache_key` from
`src/xiaozhi_openclaw_bridge/voice.py`.

Example fixed cache key:

```text
fixed/xiaoyuan/interrupt
```

## Boundary

The provider is not enabled by default. Tests use a fake HTTP response and do
not call MiniMax or consume speech quota. VPS deployment should pass the same
MiniMax key already configured for OpenClaw through service environment, not by
committing secrets to this repo.

Current VPS deployment reads the shared MiniMax key from
`/etc/xob-bridge/minimax.env` through a systemd `EnvironmentFile`. A live check
for the short text `小元` returned `audio/wav`, 29,858 bytes, with a `RIFF`
header.

The smooth XiaoZhi-like path should stream or segment inside the VPS, then send
only XiaoZhi-style OPUS packets to the board:

```text
TTS provider audio -> VPS OPUS packetizer -> board WebSocket OPUS frames
```

MiniMax TTS is deployed and usable for quality testing, but its current Bridge
integration is non-streaming WAV. It should not be treated as the final playback
path unless the MiniMax streaming API is connected or the Bridge converts its
WAV/PCM output into XiaoZhi-style OPUS packets before sending audio to the board.

Provider preference:

1. Native streaming audio from WebSocket/SSE TTS that the Bridge can packetize
   into OPUS as soon as chunks arrive.
2. Provider PCM/WAV that the VPS converts to naked OPUS packets before sending
   board frames.
3. Raw PCM/WAV over the public board WebSocket only for fallback or debugging.

Alibaba Cloud real-time TTS, Qwen-TTS/CosyVoice, Tencent Cloud streaming TTS,
and MiniMax WebSocket TTS are all viable candidates if the selected account has
usable quota and the chosen voice sounds natural enough.

Current VPS voice-chain testing uses `XOB_TTS_PROVIDER=bailian`,
`XOB_TTS_STREAMING=1`, `XOB_BAILIAN_TTS_MODEL=cosyvoice-v3-flash`,
`XOB_BAILIAN_TTS_VOICE=longyan_v3`, `XOB_WS_TTS_AUDIO_CODEC=opus`,
`XOB_TTS_SEGMENT_MAX_CHARS=60`, and `XOB_TTS_SPOKEN_MAX_CHARS=240`.

Playback finding from 2026-07-07:

- Bailian `cosyvoice-v3-flash` with `longyan_v3` can return 16 kHz PCM frames
  to the board. One measured 45,440-byte response streamed from the VPS in
  1,349 ms.
- The VB6824 output path can play the same 45,440 bytes in about 1,410 ms when
  the board buffers the whole response before enabling output.
- The Bridge should not send one oversized text block to TTS. The XiaoZhi-like
  path is sentence/segment-level: send `sentence_start`, synthesize that segment,
  packetize it as OPUS, send audio frames, then continue with the next segment.
  Hard text caps are safety guards for runaway agent output, not the main
  strategy for smooth playback.

## Validation

```bash
python3 scripts/check_tts_provider.py
```

Expected output:

```text
check_tts_provider ok
```
