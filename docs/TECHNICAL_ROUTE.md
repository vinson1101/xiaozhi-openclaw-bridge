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

## 2. 系统分层

### 2.1 板端固件层

技术选择：

- ESP-IDF。
- ST7789 直接绘制简洁状态 UI。
- NVS 保存 WiFi、Bridge 地址、配对 token、音量和默认后端。
- HTTP JSON 先连接 Bridge；WebSocket 等音频流阶段再加。
- MVP 使用按键录音，不做离线唤醒词。

首版不引入复杂 UI 框架。当前 C3 内存有限，M5Stack Avatar / StackChan 的价值是“灵动眼睛和人格化状态”，不是要求直接移植它们的库。

做法：用 ST7789 直接绘制参数化眼睛。眼睛状态由少量参数驱动：开合、瞳孔位置、眼角弧度、亮度、眨眼节奏、说话波动。

可迁移边界：

- 迁移 M5Stack Avatar / StackChan 的状态模型和交互观感：idle、listening、thinking、speaking、error。
- 迁移“眼睛参数化”的做法：开合、视线、表情、眨眼节奏。
- 不 vendor Arduino / M5GFX / M5Unified 依赖。
- 不复制 GPLv3 RoboEyes 代码；只保留它作为效果参考。
- 本仓库固件代码保持 ESP-IDF + 自写小渲染器。

### 2.2 设备协议层

传输：

- 开发期：HTTP JSON `/device/hello` 和 `/device/command`
- 音频期：`ws://bridge.local:8788/device`
- VPS 音频期：`wss://voice.example.com/device`
- 控制消息：JSON
- 音频：二进制 WebSocket frame

基础消息：

```json
{"type":"hello","device_id":"...","firmware":"...","token":"...","capabilities":["audio_in","audio_out","screen"]}
```

```json
{"type":"state","state":"idle|listening|thinking|speaking|error","text":"短文本"}
```

```json
{"type":"command.text","session_id":"...","target":"openclaw","text":"让龙虾检查今天任务状态"}
```

```json
{"type":"audio.start","session_id":"...","format":"pcm16","sample_rate":16000,"channels":1}
```

音频路线：

- MVP：PCM16 16 kHz mono，简单可靠。
- 后续：如果带宽或延迟不够，再考虑 Opus。

### 2.3 Bridge 服务层

技术选择：

- Python 3.9+。
- SQLite 作为本地事件和记忆库。
- 文本 MVP 可以用标准库 HTTP 先跑通。
- WebSocket 和音频阶段再引入 FastAPI/Uvicorn 或等价 ASGI 栈。

Bridge 职责：

- 设备配对和鉴权。
- 接收文本或音频。
- 调用 ASR。
- 选择 OpenClaw / Hermas / Zebra 后端。
- 记录 session events。
- 生成短摘要记忆。
- 调用 TTS。
- 返回文本、UI 状态和音频。

### 2.4 Agent 适配层

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
2. OpenClaw adapter：优先 HTTP，必要时 CLI 包装。
3. Hermas adapter：等真实 API 明确后接入。
4. Zebra adapter：创建/恢复 Zebra session，消费事件或轮询状态。

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
```

开发顺序：

1. Text-only：没有 ASR/TTS。
2. Mock ASR/TTS：固定文本和固定音频。
3. Remote ASR/TTS：接实际供应商。
4. 可选本地 ASR/TTS：只在 VPS 资源允许时做，不在 ESP32-C3 上做。

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
