# Phase 7C TTS Provider Adapter

This slice adds the server-side TTS provider boundary and fixed-prompt cache
keys without calling a commercial TTS service.

## Scope

- `TtsRequest`
- `TtsResponse`
- `tts_provider_for(...)`
- `FakeTtsProvider`
- `synthesize_fixed_prompt(...)`

The fake provider returns a small valid WAV file containing silence. This is
only a wiring placeholder for tests and device playback plumbing.

## Fixed Prompt Cache

`synthesize_fixed_prompt(prompt_id)` reads prompt text and `cache_key` from
`src/xiaozhi_openclaw_bridge/voice.py`.

Example fixed cache key:

```text
fixed/xiaoyuan/interrupt
```

## Boundary

This phase does not call Minimax or any other TTS provider. Dynamic answer TTS
still needs a real provider implementation after voice parameters are chosen.

## Validation

```bash
python3 scripts/check_tts_provider.py
```

Expected output:

```text
check_tts_provider ok
```
