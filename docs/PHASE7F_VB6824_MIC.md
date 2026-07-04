# Phase 7F VB6824 Microphone Path

This slice turns microphone input from hardware recon into a working firmware
probe.

## Finding

The board microphone path is not direct ESP32-C3 I2S or ADC. The stock firmware
backup contains `VbAduioCodec`, `vb6824`, and UART task strings. The open
`SmartArduino/DOIT_AI` reference implements the same `VbAduioCodec` model: the
VB6824 chip captures microphone audio, encodes 16 kHz 20 ms Opus frames, and
sends those frames to the ESP32 over UART.

The confirmed board link is:

```text
ESP32-C3 UART1
  TX GPIO20 -> VB6824 RX
  RX GPIO10 <- VB6824 TX
  baud 2000000
```

The frame format is:

```text
55 aa len_hi len_lo cmd_hi cmd_lo body checksum
```

Useful commands:

- `0x2080`: VB6824 to ESP32 audio frame, 40 bytes in the current Opus mode.
- `0x0280`: wake word text frame.
- `0x0207`: ESP32 request for wake word refresh.

## Firmware Probe

The firmware exposes:

- `:vb`: scan the likely UART pins and confirm valid VB6824 frames.
- `:vb-talk`: open the XiaoZhi-compatible WebSocket session, send
  `listen/start`, stream real VB6824 Opus frames as binary WebSocket frames,
  send `listen/stop`, then wait for `stt` and `tts` text frames.

`XOB_VB_TALK_FRAMES` is currently 12 frames, or 480 bytes. That is a short
protocol probe, not the final press-to-record loop.

## Validation

Validated on the real board:

```text
:vb
  -> tx=20 rx=10
  -> receives 0x2080 audio frames
  -> receives wake word text frame

:vb-talk
  -> websocket hello complete
  -> vb6824 websocket audio sent frames=12 bytes=480
  -> websocket stt received
  -> websocket talk probe complete
```

The Bridge still routes those bytes through the current fake ASR provider. Real
Opus decode or streaming ASR is the next server-side step.

## References

- `SmartArduino/DOIT_AI`: `main/audio_codecs/vb6824_audio_codec.cc`
- `SmartArduino/DOIT_AI`: `components/vb6824/vb6824.c`

## Boundary

This phase does not add button-driven recording, outbound TTS playback, speaker
output, or real ASR. It only proves that the real microphone frames can reach
the Bridge over the intended WebSocket path.
