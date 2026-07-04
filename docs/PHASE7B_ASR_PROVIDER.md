# Phase 7B ASR Provider Adapter

This slice adds the server-side ASR provider boundary without selecting a paid
or cloud ASR service yet.

## Scope

- `AsrRequest`
- `AsrResponse`
- `asr_provider_for(...)`
- `FakeAsrProvider`

The fake provider is only for local wiring checks. It returns
`XOB_FAKE_ASR_TEXT` when set, otherwise `你好，检查链路`.

## Boundary

This phase does not upload real audio to any external provider. It also does
not implement board-side recording, PCM upload, or audio playback.

## Validation

```bash
python3 scripts/check_asr_provider.py
```

Expected output:

```text
check_asr_provider ok
```
