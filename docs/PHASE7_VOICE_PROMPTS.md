# Phase 7A Xiaoyuan Voice Prompts

This slice defines the fixed voice-pack prompts before adding ASR, TTS, or
audio playback.

## Scope

- Wake phrase product name: `小元`.
- Preferred Chinese trigger phrase for future ASR wake handling: `你好，小元`.
- On the current VB6824 board, firmware can react to `小元` command frames, but
  the VB6824 voice pack must also recognize that spoken phrase. If the module is
  still on the default XiaoZhi wake pack, board validation may still require a
  VB6824/CozyLife voice-pack update through the firmware `:vb-ota <code>`
  serial entry.
- Fixed prompts live in `src/xiaozhi_openclaw_bridge/voice.py`.
- Each prompt has:
  - `prompt_id`
  - short Chinese `text`
  - target `eye_state`
  - stable `cache_key`

## Fixed Prompt Events

Required events:

- `wake`
- `listen`
- `confirm`
- `interrupt`
- `error`
- `setup_start`
- `setup_done`

`interrupt` maps to the same listening eye state as a fresh listen entry. This
keeps the middle-button interruption behavior aligned with the UI state model.

## Boundary

This phase does not call Minimax or any other TTS provider. It only defines the
canonical prompt texts and cache keys that a later TTS provider can synthesize.

## Validation

```bash
python3 scripts/check_voice_prompts.py
```

Expected output:

```text
check_voice_prompts ok
```
