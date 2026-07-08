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
- `0x0180`: VB6824 offline voice command text frame.
- `0x0105`: VB6824 OTA start response.
- `0x0280`: configured wake word refresh response.
- `0x0205`: ESP32 request to enter VB6824 OTA mode.
- `0x0207`: ESP32 request for wake word refresh.

## Firmware Probe

The firmware exposes:

- `:vb`: scan the likely UART pins and confirm valid VB6824 frames.
- `:vb-talk`: open the XiaoZhi-compatible WebSocket session, send
  `listen/start`, stream real VB6824 Opus frames as binary WebSocket frames,
  send `listen/stop`, then wait for `stt`, `tts` text frames, and returned TTS
  audio bytes.
- `:vb-ota <code>`: ask the VB6824 to enter OTA mode, set the official
  voice-pack authorization code, start `jl_ota_start()` when the module replies
  with `0x0105`, and feed OTA UART bytes through `jl_ondata()`.
- background wake listener: after Bridge hello, keep VB6824 UART open and map
  offline command frames containing `小元` or `小智` into the same listening and
  WebSocket voice path.
- middle listen button: mirrors upstream XiaoZhi `ToggleChatState()` behavior:
  short press in idle enters `listen/start` with `mode:auto`; listening should
  end by the auto endpointer, while another short press cancels/closes listening
  back to idle; thinking/speaking short press aborts the current turn and
  should re-enter listening. The current firmware only approximates this with a
  bring-up state machine, so listening cancel and speaking interrupt remain
  explicit real-board validation targets.

`:vb-talk` keeps `XOB_VB_TALK_PROBE_FRAMES=150`, or about 3 seconds of 20 ms
VB6824 Opus frames, as a serial probe only. Button and wake paths use
`XOB_VB_TALK_AUTO_MAX_FRAMES=6000`, about 120 seconds, only as a no-speech and
runaway safety cap. The actual endpointer decodes VB6824 Opus input frames,
waits for enough speech, then stops when the rolling tail window is mostly
silent for about 1.5 seconds. A second middle-button press while listening now
cancels listening instead of submitting the turn.

## Validation

Validated on the real board:

```text
:vb
  -> tx=20 rx=10
  -> receives 0x2080 audio frames
  -> receives configured wake word response

:vb-talk
  -> websocket hello complete
  -> vb6824 websocket audio sent frames=150 bytes=6000
  -> websocket stt received
  -> websocket talk probe complete
```

That early probe used the fake ASR provider and is kept as historical transport
evidence. Current voice-loop validation should use the Bailian ASR/TTS VPS path
with the firmware Opus-frame endpointer.

After moving the wake-triggered voice loop into a separate session task, macOS
Chinese TTS playback of the stock phrase `你好小智` validates the automatic wake
path through the reachable VPS:

```text
vb6824 voice command len=12 text=你好小智
xiaoyuan wake source=vb6824
websocket hello complete
websocket listen start sent
vb6824 websocket audio sent frames=150 bytes=6000
websocket listen stop sent
websocket stt received
vb6824 playback start wav bytes=3244 pcm bytes=3200
vb6824 playback pcm bytes=3200
websocket tts audio received bytes=3244
websocket tts audio played bytes=3200
websocket talk probe complete
```

The user's human-speech test still did not wake the device. That no longer
points to VPS connectivity, because the Mac playback test proves the wake-to-VPS
path; it points to wake phrase pronunciation, distance, microphone level, or the
VB6824 wake model/voice pack.

The first wake-listener build was compiled and flashed to the board. Startup
reached WiFi, Bridge hello, and the VB6824 listener. The board then reported the
configured wake word as:

```text
vb6824 configured wake word len=12 text=你好小智
```

So the ESP32 path is ready to consume `小元` frames, but this specific board is
still carrying the default XiaoZhi VB6824 wake pack until the wake word is
updated.

Earlier synthetic-pronunciation test on the flashed board:

```text
macOS TTS: 你好，小元
VB6824: vb6824 voice command len=12 text=你好小智
XOB:    xiaoyuan wake source=vb6824
```

This only proved that one synthetic pronunciation could hit the default XiaoZhi
model. It is not evidence that native Xiaoyuan wake works. Native Xiaoyuan wake
remains unverified until the VB6824 voice pack is updated.

The firmware now has the board-side OTA entry point for that update:

```text
:vb-ota <xiaoyuan_voice_pack_code>
```

The public restore codes found so far cover other built-in wake words such as
`你好小智` and `你好小禾`, but not `你好小元`. Do not use a non-Xiaoyuan code
unless intentionally changing the product wake word.

For the MVP, do not block the voice loop on a missing Xiaoyuan authorization
code. Keep the stock `你好小智` VB6824 wake phrase, while keeping Xiaoyuan as the
product persona and later voice-pack target.

## Current Smooth Playback Route

The only current playback route that should be treated as meaningful for
experience work is:

```text
Agent spoken text
  -> Bridge sentence/length segmentation
  -> TTS provider per segment
  -> VPS ffmpeg/libopus packetization
  -> WebSocket binary OPUS packets
  -> ESP32-C3 OPUS decode to 16 kHz mono PCM
  -> firmware playback ringbuffer
  -> VB6824 0x2081 PCM frames at fixed 10 ms cadence
```

This matches the original XiaoZhi/DOIT playback shape: incoming network audio is
not played synchronously from the WebSocket receive loop, and WebSocket frame
timing is never speaker timing. The firmware uses a VB6824 playback ringbuffer
plus a dedicated playback task, so WebSocket receive can keep accepting TTS
frames while audio drains to UART. WAV/PCM binary frames remain accepted only as
a debug fallback, not as the target voice-experience path.

The current stable build pads the beginning of each playback session with two
10 ms silent VB6824 PCM frames. This is only an onset guard for the first spoken
syllable; it does not change TTS segmentation, WebSocket framing, or playback
queue ownership.

Do not make the voice path acceptable by hard-truncating TTS text. For voice
mode, the Bridge should ask the OpenClaw agent to generate a short, plain spoken
answer at the source: one or two Chinese sentences, no Markdown, no lists, and
no emoji. If the agent still returns a longer answer, the Bridge chunks it before
TTS; `XOB_TTS_SPOKEN_MAX_CHARS` is only a runaway safety cap. The full long-form
agent behavior remains appropriate for text channels, but raw long Markdown
should not be sent directly to TTS.

Earlier fake WAV/raw PCM probes are historical bring-up evidence only. Do not
use them to decide whether the current voice playback route is good.

## Huntmind Agent Path

OpenClaw on the VPS exposes a CLI agent entry that can run a direct agent turn
without going through Feishu or Telegram:

```text
openclaw agent --agent huntmind --message ... --session-key ...
```

The Bridge service maps `target=huntmind` to that CLI path through a restricted
local wrapper. A direct two-turn smoke test validated that `huntmind` keeps
session context through the explicit session key.

For device voice sessions, the Bridge appends a spoken-output instruction before
calling the same CLI agent. A live check against HuntMind changed the earlier
long Markdown answer to a short spoken reply:

```text
我叫 HuntMind，是一个招聘决策 Agent，专门帮 HR 和猎头判断候选人值不值得联系、怎么聊效率更高。
```

The board now supports changing only the route target with:

```text
:target huntmind
```

The AP provisioning page also treats `default_target` as a free-form route name,
not as a WiFi choice. This keeps future OpenClaw agent/profile names from being
blocked by a hardcoded dropdown.

The final flashed board has `default_target=huntmind` and validates the full
transport path:

```text
config: bridge_url=configured device_token=configured wifi_ssid=configured target=huntmind
websocket hello complete
websocket listen start sent
vb6824 websocket audio start tx=20 rx=10 frames=150
vb6824 websocket audio sent frames=150 bytes=6000
websocket listen stop sent
websocket stt received
vb6824 playback start wav bytes=3244 pcm bytes=3200
vb6824 playback pcm bytes=3200
websocket tts audio received bytes=3244
websocket tts audio played bytes=3200
websocket talk probe complete
```

Agent turns can take tens of seconds, so the firmware WebSocket receive timeout
is now 180 seconds instead of the earlier short protocol-probe timeout.

This transport path was first proven with fake ASR, then with Alibaba Cloud
Bailian/DashScope as the real ASR path. Fun-ASR-Flash was the first bring-up
provider; the current VPS service uses
`XOB_ASR_PROVIDER=bailian_paraformer_realtime` with
`XOB_BAILIAN_PARA_ASR_MODEL=paraformer-realtime-v2`, using an environment file
outside Git.

The latest controlled board run validated the real chain:

```text
:vb-talk
  -> vb6824 websocket audio sent frames=150 bytes=6000
  -> websocket stt received
  -> VPS transcribed: 测试成功。
  -> VPS called OpenClaw huntmind
  -> vb6824 playback pcm bytes=126690
  -> websocket tts audio received bytes=128186
  -> websocket tts audio played bytes=126690
  -> websocket talk probe complete
```

The current user-visible path now follows the XiaoZhi interaction contract:
`idle -> listening`, listening ends by the Opus-frame endpointer, a listening
short press cancels back to idle, and thinking/speaking short press
aborts and restarts listening. For no-interrupt auto conversation, TTS playback
completion now re-enters listening on the same WebSocket session. If no speech
follows, the firmware times out silently and returns idle without submitting an
empty ASR turn.

2026-07-07 controlled continuous-dialogue validation on the flashed board:

```text
macOS TTS wake: 你好小智
turn 1 speech: 你是谁
  -> websocket vad stop frames=138 speech=31 tail_speech=3 peak=1723
  -> VPS ASR text=你是谁？
  -> websocket talk turn complete turn=1
  -> websocket continuous re-enter listening turn=2
turn 2 speech: 你能连续听我说话吗
  -> websocket vad stop frames=209 speech=84 tail_speech=3 peak=2472
  -> VPS ASR text=嗯。你能连续听我说话吗？
  -> websocket talk turn complete turn=2
  -> websocket continuous re-enter listening turn=3
turn 3 silence:
  -> websocket listen ended without speech frames=250 peak=0 timeout=1
  -> websocket continuous listen idle turn=3
```

Both spoken turns used the same server session id
`37f4781a-ca98-47d8-a20e-42ac98a690f6`.
That validation used the earlier 5-second no-speech safety cap; the current
firmware timeout is aligned to XiaoZhi-scale idle timing at about 120 seconds.

The follow-up human tests on the current stable build passed the no-interrupt
continuous dialogue flow after the LISTENING display recovery fix. A later
real-board round passed the old physical middle-button submit and
speaking-interrupt path after switching interrupt handling to same-WebSocket
`abort`; the listening branch has since been changed to cancel-to-idle and
latest VPS logs show normal board communication after that change: one
WebSocket session opened, multiple ASR -> HuntMind -> TTS turns completed, and
the session closed normally without a reboot loop.

2026-07-07 log review of failed middle-button/dialogue tests:

```text
middle button -> button mask=0x02 -> websocket hello/listen/audio/stop
recent failed turns -> ASR status=error text=<empty> at 150 VB6824 frames
same service window -> successful turns transcribed spoken Chinese at 164/178 frames
failed turns still returned short TTS and completed VB6824 playback
```

The fix is to make button/wake capture end by decoded-speech VAD instead of by
the old fixed 150-frame submit window. Do not treat this as evidence that the
middle button GPIO, WebSocket route, HuntMind route, OPUS downlink, or VB6824
playback path is down.

## Original XiaoZhi/DOIT Check

The current `SmartArduino/DOIT_AI` upstream source confirms that DOIT VB6824
boards do not run arbitrary wake-word recognition in ESP32 application code.
The VB6824 driver keeps the configured wake word, initially `你好小智`, and
refreshes it from the voice chip with command `0x0207`. Board code registers
`audio_codec.OnWakeUp(...)`, compares the received command to
`vb6824_get_wakeup_word()`, and only then calls
`Application::GetInstance().WakeWordInvoke("你好小智")`.

That means the important gate is the VB6824 voice chip. The ESP32 can map a
received `小元` command frame into the listening path, but it cannot make
human-spoken `你好，小元` produce that frame until the VB6824 wake/command voice
pack itself recognizes Xiaoyuan.

The upstream XiaoZhi application layer separates user intent from transport
timing. `Application::ToggleChatState()` maps idle to listening, listening to
stop/close, and speaking to abort. Wake detection is also an interrupt path:
when a wake event arrives while speaking, it aborts the current audio flow and
returns to listening. The bridge firmware should follow that state contract
even if the current transport is still per-turn WebSocket.

For smooth TTS output, the key upstream behavior is the audio pipeline, not a
single delay value. `Application::OnIncomingAudio` pushes server packets to
`AudioService::PushPacketToDecodeQueue(...)`; `AudioService` then decodes into a
playback queue and an output task calls the codec continuously. DOIT's VB6824
driver adds another TX ringbuffer and drains it as 320-byte PCM chunks every
10 ms, so WebSocket packet timing never becomes speaker timing.

## References

- `SmartArduino/DOIT_AI`: `main/audio_codecs/vb6824_audio_codec.cc`
- `SmartArduino/DOIT_AI`: `components/vb6824/vb6824.c`
- `SmartArduino/DOIT_AI`: `main/boards/doit-ai-01-kit-lcd/doit-ai-01-kit-lcd.cc`
- `SmartArduino/DOIT_AI`: `main/boards/doit-ai-c5-kit-lcd/doit-ai-c5-kit-lcd.cc`
- `78/xiaozhi-esp32`: `main/application.cc`

## Boundary

This phase does not add real ASR. It proves that real microphone frames can
reach the Bridge over the intended WebSocket path, that returned TTS audio bytes
can reach the device, that compatible WAV/PCM frames can be passed to VB6824 for
playback, and that the board has a serial entry for VB6824 voice-pack OTA once a
Xiaoyuan authorization code exists.
