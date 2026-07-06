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

The default provider output is 16 kHz mono WAV. For WebSocket device playback,
the Bridge strips the WAV header and sends raw 16 kHz mono PCM binary frames to
the firmware.

The firmware still accepts simple WAV binary frames for compatibility, but the
normal path is raw PCM so playback does not restart at each small WAV header.

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

Alibaba Cloud TTS models are available in the user's account and can become a
second provider later. They are not required for the first real chain because
MiniMax TTS is already deployed and working.

## Validation

```bash
python3 scripts/check_tts_provider.py
```

Expected output:

```text
check_tts_provider ok
```
