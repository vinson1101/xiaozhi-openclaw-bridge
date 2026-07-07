# 技术路线设计

## 0. 路线结论

先做服务端 Bridge，再做设备模拟器，最后刷板接入。

原因很简单：当前 ESP32-C3 板的音频 pinout、codec、屏幕初始化和内存余量都有风险；OpenClaw / Hermas / Zebra 的连接闭环没有这些硬件风险。先把文本链路跑通，再接音频和固件。

目标路线：

```text
curl / simulator / firmware
        |
        v
Bridge HTTP JSON API, WebSocket later for streaming audio
        |
        v
SQLite session events + compact memory
        |
        v
OpenClaw / Hermas / Zebra adapters
```

## 1. 不走的路线

- 不走官方小智云，不依赖 `mqtt.xiaozhi.me`。
- 不把 OpenClaw / Hermas / Zebra 塞进 ESP32-C3。
- 不直接移植 MimiClaw 到当前 C3 板；MimiClaw 只作为记忆和 skill 设计参考。
- 不先写完整固件；固件在服务端协议稳定后再做。
- 不让设备保存服务端 API key；设备只保存配对 token。
- 不依赖原小智后台配置格式；新固件使用自己的 `xob` NVS namespace。

## 2. 系统分层

### 2.1 板端固件层

技术选择：

- ESP-IDF。
- ST7789 直接绘制简洁状态 UI。
- NVS 保存 WiFi、Bridge 地址、配对 token、音量和默认后端。
- HTTP JSON 连接 Bridge；WebSocket 是当前语音帧主路径。
- `default_target` 是 agent/backend 路由名，不是 WiFi。AP 配置页和串口
  `:target <name>` 都可以切换它；当前真实 VPS 验证目标是 `huntmind`。
- 中键和对话状态以原小智体验为目标，而不是以当前临时实现为目标：
  idle 短按进入 `mode:auto` listening；listening 由 VAD/endpointer 自动结束，
  也允许再次短按提交/停止当前听写；thinking/speaking 短按 abort 当前 turn
  并重新进入 listening。MVP 保留板上默认 `你好小智` 唤醒，不让缺失的
  `你好，小元` VB6824 授权码阻塞后续链路。`小元` 继续作为产品人格和后续
  语音包目标。
- 因为官方小智语音链路已被替换，声音分成固定语音包和动态 TTS：固定短句预生成并缓存，动态回答先用已有 Minimax TTS 接口做第一版 Bridge provider；机械感强的普通 TTS 只作调试 fallback。

首版不引入复杂 UI 框架。当前 C3 内存有限，M5Stack Avatar / StackChan 的价值是“灵动眼睛和人格化状态”，不是要求直接移植它们的库。

做法：用 ST7789 直接绘制参数化眼睛。眼睛状态由少量参数驱动：开合、瞳孔位置、眼角弧度、亮度、眨眼节奏、说话波动。

可迁移边界：

- 迁移 M5Stack Avatar / StackChan 的状态模型和交互观感：idle、listening、thinking、speaking、error。
- 迁移“眼睛参数化”的做法：开合、视线、表情、眨眼节奏。
- 不 vendor Arduino / M5GFX / M5Unified 依赖。
- 不复制 GPLv3 RoboEyes 代码；只保留它作为效果参考。
- 本仓库固件代码保持 ESP-IDF + 自写小渲染器。

### 2.2 配置和配网层

原固件有后台配置，但新固件不能直接依赖它。刷入自有固件后，配置由本项目接管。

配置来源优先级：

1. `xob` NVS namespace。
2. USB serial provisioning。
3. 临时 AP + 本地 HTTP 配置页。

缺失配置时不尝试连接默认 WiFi；进入 provisioning mode。屏幕显示配置状态，串口输出缺失字段名，但不输出已有 secret 值。

分区策略：

- 固件分区表必须匹配原小智布局。
- 首次刷写优先写 app slot，保留 `nvs`、`otadata`、`phy_init`、`model`。
- 长按 reset 只清除 `xob` namespace。

### 2.3 设备协议层

传输：

- 开发期：HTTP JSON `/device/hello` 和 `/device/command`
- 音频期：小智兼容 WebSocket `ws://bridge.local:8788/device/ws`
- VPS 音频期：小智兼容 WebSocket `wss://voice.example.com/device/ws`
- 控制消息：JSON
- 音频：二进制 WebSocket frame，格式优先兼容小智 Opus frame

基础消息：

```json
{"type":"hello","version":1,"transport":"websocket","audio_params":{"format":"opus","sample_rate":16000,"channels":1,"frame_duration":60}}
```

```json
{"session_id":"...","type":"listen","state":"start","mode":"auto"}
```

```json
{"session_id":"...","type":"stt","text":"让龙虾检查今天任务状态"}
```

```json
{"session_id":"...","type":"tts","state":"sentence_start","text":"正在检查"}
```

音频路线：

- 调试探针：HTTP `/device/audio` 可上传短 PCM16，用来验证网络和鉴权。
- 主路径：按小智原框架使用 WebSocket JSON 控制帧 + 二进制 Opus 音频帧。
- 当前可流畅播放的有效路线只有这一条：
  1. Bridge 先把口播文本按标点和长度拆成 XiaoZhi-style `sentence_start`
     片段，不把长文本一次性丢给 TTS。
  2. 每个片段由 TTS provider 合成后，在 VPS 侧用 `ffmpeg/libopus` 转成裸
     OPUS packets，通过 WebSocket binary frame 下发。
  3. 固件按 server hello 的 `format=opus` 解码 OPUS 为 16 kHz mono PCM。
  4. PCM 进入板端播放 ringbuffer；VB6824 output task 以 10 ms 固定节拍发送
     `0x2081` 320-byte PCM frame。
- 不再把 raw PCM/WAV 下发到板端作为体验路线；那只保留为 debug fallback。
  也不要把 WebSocket binary frame 边界当作播放节拍。
- `XOB_TTS_SPOKEN_MAX_CHARS` 只作为异常长回复的安全上限，不是主要体验策略。
- 手动入口目标：中键短按按 XiaoZhi `ToggleChatState()` 语义分派，而不是
  按底层 UART/socket 状态猜测。`idle -> listening` 发送 `listen/start`
  `mode:auto`；`listening -> stop/submit`；`thinking/speaking -> abort`
  并重新 listening。按钮/唤醒路径用 VB6824 Opus 解码后的语音能量做 endpointer：
  等到足够语音后，约 1.5 秒静音尾巴自动提交；过早二按只挂起 submit，不截断语音。
  auto/realtime 模式下 TTS 播完后自动重新 listening；如果下一轮无语音，
  按原小智量级约 120 秒静默超时回 idle，不提交空 ASR。
  串口 `:vb-talk` 仍保留固定 150 帧调试窗口。
- 唤醒入口：VB6824 UART `0x0180` 离线命令帧命中“小智/小元”后进入 listening，并复用 WebSocket 语音路径；当前 MVP 验收用默认 `你好小智`。
- 语音包入口：串口 `:vb-ota <code>` 走 VB6824 官方 OTA 库，等拿到“小元”授权码后再更新离线唤醒/命令包。
- Agent 响应可能需要几十秒。端侧 WebSocket 接收超时按 agent turn 处理，
  当前为 180 秒，而不是音频帧级短超时。
- 不新增 MQTT+UDP，除非后续确认 WebSocket 延迟或稳定性不够。

### 2.4 Bridge 服务层

技术选择：

- Python 3.9+。
- SQLite 作为本地事件和记忆库。
- 文本 MVP 可以用标准库 HTTP 先跑通。
- WebSocket hello 先用标准库实现；真正流式音频若标准库变重，再引入 ASGI 栈。

Bridge 职责：

- 设备配对和鉴权。
- 接收文本或音频。
- 调用 ASR。
- 选择 OpenClaw / Hermas / Zebra 后端。
- 记录 session events。
- 生成短摘要记忆。
- 调用 TTS。
- 返回文本、UI 状态和音频。

小智式 server 不是单独的 TTS 服务。它是 voice gateway：对板子暴露
XiaoZhi-compatible WebSocket，对内编排 ASR/VAD、Agent、TTS、播放状态和
interrupt。OpenClaw、Hermas、Lobster 这类系统应该接在 Agent 适配层，提供
内容回复、工具调用和长期记忆；TTS 仍由 voice gateway 统一处理，除非某个
Agent 自身能返回可取消的实时音频流。

当前固件仍是一轮对话按一次 WebSocket session 跑完，但按钮体验必须遵守小智
状态机：start listening、endpointer 自动提交、speaking 可打断并重进 listening。
长生命周期 audio channel 可以后续替换 transport，不能改变这个交互 contract。

### 2.5 Agent 适配层

统一请求：

```json
{
  "session_id": "string",
  "target": "openclaw|hermas|zebra",
  "user_text": "string",
  "context": {
    "device_id": "string",
    "recent_memory": [],
    "mode": "voice"
  }
}
```

统一响应：

```json
{
  "status": "done|running|needs_approval|error",
  "text": "string",
  "summary": "string",
  "artifacts": []
}
```

适配顺序：

1. Fake adapter：本地回声和测试。
2. OpenClaw adapter：当前验证路径是 CLI 包装，`openclaw agent --agent huntmind --message ... --session-key ...`，服务端 Bridge 通过受限 wrapper 调用。
3. Hermas adapter：等真实 API 明确后接入。
4. Lobster adapter：按同一 `AgentRequest` / `AgentResponse` contract 接入；如果只提供同步文本，先可用但体验不会等同小智。要做到小智体验，需要支持 token 或句子级 streaming 输出，供 Bridge 边生成边送 TTS。
5. Zebra adapter：创建/恢复 Zebra session，消费事件或轮询状态。

## 3. 本地上下文和记忆

采用三层记忆：

```text
device NVS
  设备设置和少量最近状态

bridge SQLite
  session events
  compacted memories
  device preferences
  backend routing history

agent native memory
  OpenClaw / Hermas / Zebra 自己的长期记忆
```

Bridge 里的 `session_events` 是事实源；`memories` 是从事件中压缩出来的派生信息，可以重建和纠正。这个边界参考 Zebra 的事件优先架构。

最小表：

```text
devices(id, name, token_hash, created_at, last_seen_at)
sessions(id, device_id, target, status, created_at, updated_at)
session_events(id, session_id, seq, type, payload_json, created_at)
memories(id, scope, key, value, source_event_id, updated_at)
artifacts(id, session_id, kind, uri, meta_json, created_at)
```

## 4. 安全路线

局域网开发：

- 可以先用 `ws://`。
- Bridge 绑定内网地址。
- 配对 token 必须启用。

VPS：

- 必须使用 `wss://`。
- 反向代理终止 TLS。
- 设备只持有配对 token。
- OpenClaw / Hermas / Zebra API 不直接暴露公网。
- 服务端 API key 只放 VPS 环境变量或 secret 文件。

审批：

- 语音只负责发起任务。
- 涉及交易、资金、部署、删除文件等高风险动作时，后端必须返回 `needs_approval`，由原系统的审批链路处理。

## 5. 固件路线

刷板前置条件：

1. 当前板完整 flash 备份。
2. 原固件可恢复验证。
3. 已确认串口下载和重启流程稳定。
4. 已确认屏幕、按键、麦克风、喇叭 pinout 或可从开源板配置复用。

固件阶段：

1. 只显示 UI：启动、WiFi、错误状态。
2. 连接 Bridge：发送 `hello`，显示 connected。
3. 文本命令：串口输入文本，经 Bridge 返回屏幕显示。
4. 音频上传：按键录音，Bridge 返回识别文本。
5. 音频播放：Bridge 返回 TTS，板端播放。

这条路线避免一开始就在音频驱动上阻塞。

## 6. UI 路线

MVP 状态：

- idle
- listening
- thinking
- speaking
- error

屏幕布局：

```text
[WiFi] [backend] [battery]

        animated eyes
        optional tiny mouth/wave

recognized text or short result
```

UI 参考的是 M5Stack Avatar / StackChan / RoboEyes 这类“有生命感的眼睛”，不是普通仪表盘。实际实现只迁移 MIT 项目的思路，不复制第三方源码。

眼睛状态表：

| 状态 | 眼睛表现 |
|---|---|
| idle | 慢速自动眨眼，瞳孔轻微漂移 |
| listening | 眼睛睁大，瞳孔朝向用户，底部轻微波形 |
| thinking | 瞳孔左右扫视，眨眼变慢 |
| speaking | 眼睛保持亮起，嘴型或波形随音频变化 |
| error | 眼睛变窄或不对称，低亮度 |

实现顺序：

1. 静态眼睛。
2. 自动眨眼。
3. 状态切换。
4. 视线移动。
5. 说话波形或嘴型。

不要为 C3 首版引入 LVGL 或完整 avatar 库；除非切到 ESP32-S3 + PSRAM。

## 7. ASR / TTS 路线

先抽象接口，不绑定一家服务：

```text
AsrProvider.transcribe(audio) -> text
TtsProvider.synthesize(text, voice) -> audio
TtsProvider.stream_audio(text, voice) -> Iterable[pcm16_16k_mono]
```

开发顺序：

1. Text-only：没有 ASR/TTS。
2. 固定语音包：唤醒、确认、打断、错误、配置提示等常用短句预生成并缓存。
3. Mock ASR/TTS：固定文本和固定音频。
4. Remote ASR/TTS：ASR 用 `paraformer-realtime-v2` 先接通真实链路；动态 TTS 可以先由 provider 产出 WAV/PCM，但 Bridge 到板端的 WebSocket 音频应按小智式 OPUS packets 传输，固件端再解码成 16 kHz mono PCM 给 VB6824 播放队列。
5. 可选本地 ASR/TTS：只在 VPS 资源允许时做，不在 ESP32-C3 上做。

TTS provider 选型按体验优先：

1. 首选：WebSocket/SSE 真流式输入和输出，服务端能尽早拿到音频增量，并 packetize 成 OPUS 发给板端。
2. 可接受：provider 返回 PCM/WAV，VPS 侧按句子/片段调用 TTS，用 `ffmpeg` 转成裸 OPUS packets 再发给板端。
3. 不接受作为长期方案：把 raw PCM 从 VPS 直接推到板端；带宽和抖动会破坏小智式连续播放体验。

## 8. 验证路线

每阶段只留一个最小检查：

- Text Bridge：`curl /command` 返回后端响应，并写入 SQLite event。
- Simulator：WebSocket hello、command、state 三类消息可跑通。
- OpenClaw adapter：fake 和 live 两种模式都能返回统一响应。
- Firmware UI：串口日志和屏幕状态一致。
- Audio：一条录音能得到文本，一条文本能播出语音。

## 9. 关键技术决策

| 决策 | 选择 | 原因 |
|---|---|---|
| 产品入口 | 自有 Bridge | 摆脱官方小智云 |
| 板端角色 | 语音和 UI 终端 | C3 资源有限 |
| Bridge 语言 | Python | 接 OpenClaw/Zebra 成本低 |
| 本地存储 | SQLite | 足够、可审计、无需服务依赖 |
| 实时连接 | WebSocket | 适合设备长连接和音频流 |
| 音频格式 | PCM16 first | 简单可调试 |
| UI | 直接绘制 first | C3 上少依赖 |
| Zebra 迁移 | 服务端概念迁移 | 不把重 Agent 放进固件 |

## 10. 近期落地顺序

1. `PLAN.md` 固定任务。
2. 文本 Bridge MVP。
3. SQLite event store。
4. Fake/OpenClaw adapter。
5. WebSocket simulator。
6. 固件备份和恢复验证。
7. 固件 UI + Bridge hello。
8. 音频链路。
