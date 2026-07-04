# 基本设计方案

## 0. 项目定位

本项目把一块小智形态的 ESP32 语音设备，改造成私有的语音终端，用来连接 OpenClaw、Hermas 和 Zebra 支撑的服务端 Agent。

目标系统不依赖官方小智云服务。必要时可以完整备份后刷入自有固件。

板端负责听、说、显示、按键和少量本地状态；服务端负责 ASR、TTS、路由、Agent 执行、持久记忆、工具调用和审计。

## 1. 调整后的核心判断

正确拆分是：

```text
ESP32 板端
  麦克风
  喇叭
  ST7789 屏幕
  按键 / 电源 / 电量
  少量本地上下文
        |
        v
Bridge 服务
  设备连接
  ASR / TTS
  鉴权
  会话路由
  本地事件和记忆层
        |
        v
Agent 后端
  OpenClaw
  Hermas
  Zebra runtime
```

板子不是主 Agent。它是麦克风、喇叭、显示屏和本地控制面板。

## 2. 目标

- 通过语音向局域网或 VPS 上的 OpenClaw / Hermas 下达指令。
- 不通过官方小智服务。
- 支持自定义屏幕 UI，重点参考 M5Stack Avatar / StackChan 那种有生命感的动态眼睛。
- 语音人格名使用“小元”；中文触发短语优先 `你好，小元`，备选 `小元小元`。
- 支持自定义提示音、唤醒音、错误音、固定语音包和服务端选择的 TTS 音色。
- 固定语音包用于唤醒、确认、打断、错误、配置等常用短句，可一次生成后缓存播放。
- 动态 TTS 只用于 Agent 每次生成的回答。Bridge 侧首选用已有 Minimax TTS 接口做第一版 provider，同时保留可替换 provider；普通生硬 TTS 只能作为临时 fallback。
- 本地具备基础上下文和记忆管理。
- 服务端 Agent 架构可以适当迁移 Zebra 的关键模块。
- 先适配当前 ESP32-C3 板；如果硬件资源不够，再切 ESP32-S3 + PSRAM。

## 3. 非目标

- 不把完整 OpenClaw、Hermas 或 Zebra 塞进 ESP32-C3。
- 不在 ESP32-C3 上做离线 ASR / TTS。
- 不把 API key、VPS token、flash 备份、私人记忆提交到 Git。
- 不默认 vendor `mimiclaw`、`xiaozhi-esp32` 或 Zebra 源码。
- 不把官方小智协议当成最终产品边界。

## 4. 当前硬件基线

已识别到的设备：

- 主控：ESP32-C3 QFN32 rev v0.4
- Flash：8 MB
- 网络：WiFi / BLE
- USB：原生 USB-Serial/JTAG
- 原固件：`xiaozhi` `1.6.1`
- 板型字符串：`zuowei-c3-realtime-lcd`
- 屏幕驱动字符串：`ST7789`
- 原固件连接点：`mqtt.xiaozhi.me`

设计影响：

- ESP32-C3 适合做联网语音终端。
- ESP32-C3 不适合承担 Zebra 级别的 Agent loop、长上下文和复杂记忆。
- 如果麦克风、喇叭或屏幕 pinout 难以稳定复用，应保留切换到 ESP32-S3 + PSRAM 的路线。

## 5. 固件设计

目标固件应是自有 ESP-IDF 固件，而不是官方小智云固件。

首版固件模块：

- WiFi 配网和自动重连。
- Bridge 地址配置，保存在 NVS。
- 设备身份和配对 token。
- HTTP JSON 客户端，先发送 Bridge hello；WebSocket 等音频流阶段再加。
- ST7789 屏幕状态 UI。
- 按键触发录音；未来可把 `你好，小元` 作为中文唤醒/ASR 触发短语。
- 音频上传。
- 自然中文 TTS 或音频播放。
- 电量、WiFi、错误状态上报。

板端本地状态保持很小：

- device id
- 默认后端：OpenClaw / Hermas / Zebra
- 最近连接状态
- 最近几条指令摘要
- UI 状态
- 音量和音色偏好

长期记忆放 Bridge 或 Agent 后端。

## 5.1 配置和配网

原小智固件的后台配置不能当作新固件的配置源。即使刷机时保留了原 NVS，里面的数据格式也属于原固件；新固件只读写自己的 `xob` namespace。

新固件配置项：

- `wifi_ssid`
- `wifi_password`
- `bridge_url`
- `device_token`
- `default_target`
- `volume`
- `brightness`

首版流程：

1. 开机读取 `xob` NVS。
2. 配置完整则连接 WiFi 并向 Bridge 发送 `/device/hello`。
3. 配置缺失则进入 provisioning mode。
4. 开发期先支持 USB serial 写入；正常使用再加临时 AP + 本地 HTTP 配置页。
5. 长按重置只清除 `xob` namespace，不全盘 erase。

刷机策略：

- 保留原厂分区布局。
- 不使用通用 ESP-IDF 分区模板。
- 首次刷写优先只写 app 分区。
- 全片擦除前必须再次确认 restore 路径。

## 6. 屏幕和声音

屏幕目标不是普通状态页，而是一个“可感知状态的眼睛”。

参考方向：

- M5Stack Avatar：脸部/avatar 渲染思路。
- StackChan：桌面小机器人人格化交互。
- RoboEyes：眨眼、视线移动、困惑、开心、疲惫等平滑眼睛动画。

视觉目标：

- 顶部状态栏：WiFi、后端、Token/配对、电量。
- 中间主区域：一双动态眼睛，表达空闲、聆听、思考、说话、错误。
- 底部短文本：识别出的指令或最终结果摘要。
- 聆听时眼睛看向用户或轻微放大。
- 思考时眼睛左右扫视或眨眼变慢。
- 说话时眼睛和简单嘴型/波形同步。
- 错误时眼睛变窄、低亮或显示困惑状态。

当前 ESP32-C3 首版不直接移植 M5Stack Avatar 库，而是迁移它适合本项目的部分：状态模型、眼睛参数、眨眼/视线/说话状态的交互观感。实现方式仍是 ST7789 直接绘制，少依赖、少内存、好调试。

不复制 GPLv3 RoboEyes 源码；只参考效果。

声音目标：

- MVP 使用按键唤醒，不先做离线唤醒词。
- 板端录音上传给 Bridge。
- Bridge 做 ASR，调用 Agent，再做 TTS。
- 板端播放固定语音包、TTS 和短提示音。
- 离线唤醒词只有在确认 CPU/内存余量后再加。

## 7. Bridge 服务设计

Bridge 是产品核心。

职责：

- 接收局域网或 VPS 上的设备连接。
- 使用配对 token 鉴权设备。
- 接收音频片段或文本指令。
- 调用 ASR。
- 路由到 OpenClaw、Hermas 或 Zebra。
- 记录 session event 和本地摘要记忆。
- 调用 TTS。
- 把 UI 事件和音频返回给设备。

推荐传输：

- 板端到 Bridge：先用 HTTP JSON。
- 控制消息：JSON。
- 音频帧：后续用二进制 WebSocket frame。
- 局域网开发：可以先用 `ws://`。
- VPS：必须用 `wss://`，放在反向代理后面。

不要把 OpenClaw 或 Hermas 直接暴露到公网。公网只暴露 Bridge，再由 Bridge 访问内部 Agent 服务。

## 8. Agent 后端适配

Bridge 对后端使用统一请求：

```json
{
  "session_id": "string",
  "user_text": "string",
  "target": "openclaw|hermas|zebra",
  "context": {
    "device_id": "string",
    "location": "lan|vps",
    "recent_memory": []
  }
}
```

统一响应：

```json
{
  "text": "string",
  "summary": "string",
  "status": "done|needs_approval|running|error",
  "artifacts": []
}
```

适配器：

- OpenClaw adapter：优先调用已有 HTTP 入口；没有稳定 HTTP 时用 CLI 包一层。
- Hermas adapter：先按同一 contract 预留，等确认真实 API 后实现。
- Zebra adapter：创建或恢复 Zebra session，把状态流转回 Bridge。

## 9. 可迁移的 Zebra 模块

迁移到 Bridge / 服务端，不迁移进固件：

- append-only session event store
- session projection
- context compiler
- typed tool gateway
- policy checks
- credential boundary
- artifact / audit records

首版可以用 SQLite 做最小数据层：

```text
sessions
session_events
device_pairings
memories
artifacts
```

后续如果 Zebra 服务成熟，Bridge 可以只保留设备协议和语音能力，把 Agent 执行交给 Zebra。

## 10. 本地上下文和记忆

分三层：

1. 板端记忆：NVS 里的设备配置、最近状态、少量指令摘要。
2. Bridge 记忆：session events、压缩摘要、设备偏好、用户偏好。
3. Agent 记忆：OpenClaw / Hermas / Zebra 自己的长期记忆。

MimiClaw 的价值是参考本地文件式记忆和 skill 思路，但当前 ESP32-C3 不应承载完整 MimiClaw 记忆系统。

## 11. 部署模式

局域网模式：

```text
board -> http://bridge.local:8788 -> local OpenClaw/Hermas/Zebra
```

VPS 模式：

```text
board -> https://voice.example.com -> VPS Bridge -> OpenClaw/Hermas/Zebra
```

没有局域网 Agent 时，VPS 模式完全可行。板端只需要能出网。

## 12. 里程碑

1. 设计基线和原 flash 备份。
2. 文本 Bridge：HTTP 文本输入，后端文本输出。
3. Bridge event store：SQLite 保存 session 和 event。
4. 设备模拟器：HTTP JSON 发送 hello 和文本命令。
5. 自有固件骨架：WiFi、配对、Bridge hello、屏幕状态。
6. OpenClaw adapter。
7. Hermas adapter。
8. 音频链路：录音、ASR、路由、TTS、播放。
9. Zebra runtime adapter。
10. 固件打磨：UI、提示音、设置、断线重连。

## 13. 主要风险

- 当前板子的音频 codec 和 pinout 可能没有公开资料。
- ESP32-C3 内存对音频流加 UI 可能偏紧。
- 刷机前必须有可恢复的完整 flash 备份。
- VPS 模式必须有 TLS 和配对机制，不能裸 HTTP 暴露。
- OpenClaw / Hermas 需要稳定服务 API，否则语音体验会不稳定。

## 14. 第一个验收 Demo

先做文本闭环：

```text
curl /command "让龙虾检查今天任务状态"
  -> Bridge 创建 session event
  -> Bridge 调用 OpenClaw
  -> Bridge 返回中文短回答
  -> Bridge 保存 event 和摘要
```

这个通过后，再开始固件音频链路。
