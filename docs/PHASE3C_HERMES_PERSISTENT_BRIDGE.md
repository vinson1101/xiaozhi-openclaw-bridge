# Hermes 分支升级任务书 v2：Persistent Agent Bridge

更新日期：2026-10-18

## 1. 升级结论

继续当前“小智板端 -> Bridge -> Hermes 小元”的路线，不重做板端语音栈，也不
改变 Hermes 服务器已有的业务能力。升级重点只有一个：把当前“每轮启动一次
`hermes chat`”的兼容链路，升级为常驻 Agent Bridge 和可复用的 `AIAgent`
会话，消除每轮 CLI 冷启动，并补齐进度、审批、中断和错误事件。

当前 `hermes-cli` 路线继续保留，作为已验证的连通性基线和故障降级路径，不再
作为最终产品展示架构。

## 2. 本次绝对边界

本次升级只改 Bridge、Hermes 接入适配层及其部署配置，不修改 Hermes 服务器上
已经存在的业务内容。

| 范围 | 本次是否允许修改 | 说明 |
|---|---:|---|
| ESP32 到 Bridge 的 ASR/TTS/OPUS 协议与 provider | 保持兼容 | 允许按下文优先级改进调度、队列和流式能力，保留现有协议及 provider |
| Bridge 的 Hermes adapter、事件协议和会话映射 | 是 | 本次主要工作区 |
| 新增主机本地常驻 broker/worker 服务 | 是 | 只承载 Hermes 调用，不承载产品业务规则 |
| `~/.hermes/skills/**` | 否 | 不改 Skill 内容、描述、触发条件或工具约束 |
| 公司介绍、产品介绍、PPT/视频及关联资产 | 否 | 不改文案、媒体、播放顺序或展示流程 |
| HiBrush 等产品 runner/browser 自动化 | 否 | 沿用现有执行与 one-submit 安全契约 |
| Hermes persona、模型、审批和 Computer Use 策略 | 否 | 继续由 Hermes 服务器现有配置负责 |
| 现有公司介绍、HiBrush 的 harness 链路 | 否 | 保持入口、话术/别名规则、Skill 预加载、toolsets、session 绑定及执行顺序不变 |

服务器部署前后必须对受保护目录、产品资产、harness/launcher、runner 和相关
配置做只读清单/摘要校验；只读核对有效入口和参数，不记录凭据。文件摘要或业务
调用契约变化就停止切换。本次升级不得通过改路由、补别名或调整 Skill 来适配新接口。
这里的受保护配置指现有 Hermes 业务链路配置；新增 Bridge/传输配置独立存放，
不得覆盖现有业务配置。

## 3. 谁理解语音任务，谁保证 Skill 命中

LLM 应该根据最终语音转写和当前会话理解用户具体想做什么，但它不能成为公司
介绍、HiBrush 等受保护业务的唯一 Skill 路由器。之前已经出现过 LLM 没有命中
所需 Skill 而走偏的情况，因此正式链路必须保留 harness 的确定性路由和 session
绑定；LLM 在正确 Skill 已加载后理解具体语义、参数和后续指代。

```text
用户语音
  -> Bridge ASR 得到最终转写
  -> Bridge 选择 target=hermes，并传递原始任务、session 和通道信息
  -> host-local harness 检查受保护业务路由和已有 session 绑定
  -> 命中公司介绍/HiBrush 等路由时，预加载指定 Skill 和有限 toolsets
  -> Hermes LLM 在该 Skill 契约内理解任务、参数和上下文
  -> 既有 Skill/runner 按现有安全契约执行
  -> Hermes 返回进度、审批、结果或错误事件
  -> Bridge 负责状态显示、分句 TTS、OPUS 播放和 abort
```

职责必须保持单一：

- Bridge 只做 Agent 目标选择，不在板端或 Bridge adapter 中维护产品路由。
- host-local harness 负责公司介绍、HiBrush 等受保护业务的确定性 Skill 命中、
  toolset 限制、fail-closed 和 session route affinity。
- 常驻 adapter 只做传输、会话和事件转换，并携带 harness 给出的 route metadata，
  不硬编码产品执行步骤。
- Hermes LLM 在已命中的 Skill 契约内负责理解用户语义、抽取参数、追问，以及理解
  “刚才那个”“换一个”“继续播放”等后续表达。
- 已有 Skill/runner 负责确定性执行、参数校验和副作用边界。
- 含糊、不可逆、外部提交或高风险任务必须先确认；“LLM 能理解”不等于可以绕过
  审批和 one-submit 契约。

受保护业务的路由优先级为：

1. 当前语音明确命中 harness 中的受保护业务路由；
2. 当前语音是省略产品名的后续任务时，复用该 session 已绑定的 route；
3. 疑似受保护业务但无法可靠命中时，要求澄清，不得静默落到普通问答；
4. 确认不属于受保护业务后，才进入 Hermes 的开放式通用对话/工具判断。

当前 `hermes_cli_with_desktop_env.sh` 已实现受保护业务的初始路由和 session 绑定。
Phase 3C 不把它降级成仅供 fallback 使用，而是把同一份 harness route contract
接到常驻 worker；通过外层接入适配复用现有行为，不修改其话术/别名、路由、
Skill 预加载、toolsets、session 绑定或 runner。若常驻接口必须改变现有 harness
链路才能工作，该方案不通过接口选择验收，保留当前路径并记录具体不兼容点。

## 4. 架构目标

```text
XiaoZhi ESP32
  <-> Bridge WebSocket voice session
  <-> HermesBridgeAdapter
  <-> local IPC/TCP
  <-> long-lived Hermes broker
  <-> reusable profile worker
  <-> reusable AIAgent session
  <-> existing Hermes Skills / tools / product assets
```

借鉴 Hermes Studio 的点是常驻 broker、按 profile 复用 worker、复用真实 Agent
对象、事件接口和空闲回收；不照搬其 Web UI，也不把 Hermes Studio 变成板端
gateway。参考：

- [Hermes Agent v0.14.0 cold-start release notes](https://github.com/NousResearch/hermes-agent/releases/tag/v2026.5.16)
- [Hermes Studio chat session architecture](https://github.com/EKKOLearnAI/hermes-studio/blob/main/docs/cli-chat-sessions.md)
- [Hermes Studio Agent session pool](https://github.com/EKKOLearnAI/hermes-studio/blob/main/packages/server/src/services/hermes/agent-bridge/python/bridge_pool.py)

Hermes 上游减少约 19 秒启动时间，证明 CLI 冷启动是已知问题；但升级 Hermes
版本不能替代常驻架构。正式链路必须证明 warm turn 不再创建新的 CLI/Python
进程。Hermes Studio 的内部协议和代码只作架构参考；复用任何实现前先确认公开
接口、稳定性和许可边界。

### 4.1 借鉴 `78/xiaozhi-esp32` 的优先级

对比基线：上游 [e0d2390（2026-10-17）](https://github.com/78/xiaozhi-esp32/tree/e0d2390a1ca415473508bbc0c66eaf1d8dc04e37)；
本仓库 GitHub `main=060430a`、Hermes 分支 `ea0da69`。以下均为待实施任务，
不能作为已部署能力。优先级决定实施顺序，Gate 0–5 继续负责证据和切换验收。

| 优先级 | 借鉴内容与落点 | 最小验收 |
|---|---|---|
| P0 | Gate 0 冻结服务器 Skill、公司介绍、HiBrush harness/runner 和配置，记录分段延迟 | 只读清单、摘要和调用契约基线齐全；所有后续阶段沿用 |
| P1.1 | 借鉴上游事件调度，先解耦 WebSocket 收包和后台 ASR/Agent/TTS | 人为阻塞 Agent 时，同连接 abort 仍先被处理；不必等待 Agent 完整回复 |
| P1.2 | 借鉴 AudioService 的有界队列、播放 generation、排空通知，补齐 Gate 4 | 中断后迟到解码结果不能重新入队；正常尾音播完才重新收音；连续 10 次中断无串轮和队列持续增长 |
| P1.3 | 延续 Hermes 常驻 Agent 方案，完成 Gate 1–3，并接上 P1.1/P1.2 的取消链 | 10 个 warm turns 不逐轮启动 CLI；harness/Skill 选择和业务调用契约保持不变 |
| P2.1 | 补齐 Agent 可朗读片段 -> TTS -> 增量 Opus 下发，随后评估流式 ASR | 首段音频早于整段回答完成；p50/p95 分段计时可对比；partial transcript 不触发业务执行 |
| P2.2 | 补板端 WSS/TLS 与连接错误恢复；这是公网启用前置条件，不因排在 P2 而豁免 | 受信证书连接成功，错误证书拒绝，断连后恢复；禁止明文降级或跳过证书验证 |
| P3 | 换板或增加明确设备控制需求时，评估复用上游固件/设备 MCP | 先验证 VB6824、屏幕和管脚兼容；当前不新增移植或 MCP 实现任务 |

上游可直接参考的实现：

- [AudioService](https://github.com/78/xiaozhi-esp32/blob/e0d2390a1ca415473508bbc0c66eaf1d8dc04e37/main/audio/audio_service.cc)：
  采集、编解码、播放任务解耦，有界队列，`playback_generation_` 与排空回调。
- [Application](https://github.com/78/xiaozhi-esp32/blob/e0d2390a1ca415473508bbc0c66eaf1d8dc04e37/main/application.cc)：
  主循环处理状态事件，auto 模式等待实际播放排空后才启动收音。
- [WebSocket 协议](https://github.com/78/xiaozhi-esp32/blob/e0d2390a1ca415473508bbc0c66eaf1d8dc04e37/docs/websocket.md)：
  保持 JSON 控制和二进制 Opus、能力协商、listen/abort/STT/TTS 语义兼容。

当前代码差距：`listen/stop` 后的同步 Agent 调用会阻塞收包；Opus TTS 分支先
合成整句再下发；自写板端 WebSocket 连接只接受 `http://` Bridge URL。不能把
“已有 WebSocket”“provider 支持 streaming”或 nginx TLS 配置视为这些差距已解决。

不列入当前实施：MQTT+UDP、AEC/全双工、大规模板型支持、全面替换现有固件。
上游已迁移 ESP-IDF 6.x，本项目记录的构建基线是 5.3.5，不能直接混入其依赖或
整体照搬源码；按当前板型逐项采用机制。

## 5. 分阶段任务

### Gate 0：冻结边界和建立基线

- [ ] 只读记录 Hermes 版本、启动方式、当前 Bridge 服务配置和桌面会话环境。
- [ ] 只读记录已保护的 Skill、公司/产品介绍资产、现有 harness/launcher、runner
  和相关配置的文件清单、摘要及调用契约。
- [ ] 用固定的简单对话、三轮上下文、HiBrush prepare-only 和公司介绍
  dry-run/只读场景建立 CLI 基线。
- [ ] 分段记录 `ASR final -> Agent accepted -> first speakable -> final -> first
  audio`，至少统计 warm/cold 的 p50 和 p95。

验证：本阶段不写 Hermes 服务器业务目录；基线报告能够区分进程启动、模型推理、
工具执行和 TTS 时间。

### Gate 1：确认常驻接口

- [ ] 优先确认 Hermes 官方是否提供稳定的 Python、HTTP、WebSocket、SSE 或本地
  broker 接口，以及是否能复用真实 `AIAgent` 对象。
- [ ] 确认常驻 worker 能在不修改现有 harness 的前提下复用其路由、Skill 预加载、
  toolsets 和 session 绑定；route metadata 如需转换，由新增接入层承担。
- [ ] 如果复用 Hermes Studio bridge，先确认协议稳定性和许可；如果不适合复用，
  实现最小主机本地服务，但不复制受限源码。
- [ ] 最小协议只包含 `chat`、`get_output`/stream、`interrupt`、`approve`、
  `clarify`、`status` 和 `destroy`。
- [ ] 禁止解析交互式 CLI 屏幕输出来伪造事件流。

验证：同一个 broker 连续处理 10 个 turn，启动计数保持为 1；同一 Bridge session
复用同一个 Hermes Agent session。

### Gate 2：实现 Bridge adapter

- [ ] 新增独立的 `hermes-bridge` adapter kind，不破坏现有 `hermes-cli`。
- [ ] 使用配置选择常驻路径或 CLI fallback，不在代码里写死主机、Skill、产品名或
  桌面权限。
- [ ] 让 host-local harness 在进入 broker 前完成受保护业务路由；Bridge adapter
  只传递原始任务和 route/session 结果，不复制 harness 的产品规则。
- [ ] 映射 `device_session_id -> bridge_session_id -> hermes_session_id`，支持
  创建、恢复、空闲回收和显式结束。
- [ ] broker/worker 异常退出时限次重启；失败后可以显式降级到 CLI，但同一任务
  不得自动重复产生副作用。
- [ ] 参考 Hermes Studio 设置可配置的空闲回收，第一版默认 30 分钟。

验证：10 个 warm turns 没有每轮 `subprocess.run(hermes chat ...)`；重启 Bridge 后
能够按约定恢复或明确结束会话，不产生两个活跃执行。

### Gate 3：harness 保证路由，LLM 理解具体任务

- [ ] Bridge 原样传递最终 ASR 文本、会话标识和必要的通道元数据；产品路由只在
  host-local harness 维护，不复制到 Bridge adapter。
- [ ] 语音输出长度/格式属于 response channel contract，不与产品业务提示混写。
- [ ] 公司介绍、HiBrush 等受保护话术必须由 harness 命中指定 Skill；Skill 缺失、
  route 冲突或疑似命中时 fail closed 或要求澄清。
- [ ] Hermes LLM 在已绑定 Skill 内，根据自然语言和多轮上下文理解具体操作、参数
  和指代，不依赖固定模板回答。
- [ ] 为每个受保护业务验证规范话术、已批准别名和 session follow-up 三类表达。
- [ ] 对“刚才那个产品”“换一个”“继续播放”等指代/省略表达验证会话理解。
- [ ] 含糊或高风险任务先澄清/审批，不允许 adapter 猜测后直接执行。

验证：首次受保护业务请求由 harness 命中正确 Skill；后续省略产品名的请求复用
session route；LLM 正确理解 Skill 内参数。Bridge adapter 中没有公司介绍、
HiBrush 或具体产品规则，疑似业务请求不会误落到普通回答。

### Gate 4：事件、语音反馈和中断

- [ ] 统一转发 `started`、`speakable`、`tool_progress`、`approval`、`clarify`、
  `final`、`error` 和 `cancelled`。
- [ ] 借鉴 [Hugging Face Realtime Engine](https://github.com/huggingface/speech-to-speech/blob/5b443c8011349f7b5a7138c2ad29fe1c1e65b8bd/src/speech_to_speech/api/openai_realtime/README.md)
  给每个活跃 turn 分配递增 generation；Agent、文本和 TTS 输出必须携带同一
  generation，旧 generation 的迟到输出在写入设备 WebSocket 前丢弃。
- [ ] 长任务先返回可朗读的开始/进度事件，避免板端静默等待完整结果。
- [ ] WebSocket 读循环不能等待 Hermes/TTS 完成；`abort` 同时停止当前 TTS 和
  Hermes 活跃 run、清空本轮待播队列，但保留已经完成的会话上下文。
- [ ] 复用板端已有播放 ringbuffer，补有界队列和背压；decoder reset/abort 后丢弃
  正在处理的旧 generation 结果，避免清空后重新入队。
- [ ] 正常 `tts/stop` 只表示网络发送结束；auto 模式等待解码、播放队列和正在输出
  的尾帧排空后再收音，通过事件通知主循环，避免阻塞主循环或截断尾音。
- [ ] 事件携带稳定的 session/run/event 标识，重复事件不会触发重复播报或重复执行。

验证：长任务能先播报进度；中断后本轮停止，下一轮仍能使用同一会话；没有重复
提交产品任务。

### Gate 5：回归、切换和回滚

- [ ] 先在独立 target/端口部署常驻 adapter，CLI 路线继续可用。
- [ ] 运行普通问答、三轮上下文、HiBrush prepare-only、公司介绍只读/经批准播放、
  approval、abort 和异常恢复用例。
- [ ] 部署后重新计算受保护 Skill、产品介绍资产、harness/launcher、runner 和配置
  摘要，与 Gate 0 对比，并验证实际路由、预加载、toolsets 和 session 绑定未变。
- [ ] 比较常驻路径和 CLI 基线；warm path 的 adapter dispatch 中位数不高于
  500 ms，并且同组简单对话端到端 p50 必须优于 CLI 基线。
- [ ] 验收后才把 `target=hermes` 切到常驻 adapter；保留一条配置级 CLI 回滚路径。

验证：受保护文件摘要完全一致，所有语音/会话用例通过，回滚不需要修改固件或
服务器 Skill。

## 6. 总体验收标准

- [ ] 当前板端、Bridge ASR/TTS/OPUS 协议和 provider 保持兼容，调度/队列升级按
  P1 验收，端到端流式能力按 P2 单独验收。
- [ ] warm turn 复用常驻 broker、worker 和 Agent session，不再每轮启动 CLI。
- [ ] harness 保证公司介绍、HiBrush 等受保护业务命中指定 Skill；LLM 在已绑定
  Skill 内根据语音语义和上下文理解具体任务。
- [ ] 三轮上下文、进度、审批、中断和错误都能通过 Bridge 正确反馈。
- [ ] 现有 Skill、公司/产品介绍、媒体资产、harness/launcher、runner 和配置
  部署前后摘要及业务调用契约一致。
- [ ] CLI fallback 可用，失败或超时不会自动重复提交具有副作用的任务。
- [ ] 实测延迟报告能够证明改善来自消除进程冷启动，而不是隐藏等待或缩短回答。

## 7. 本阶段交付物

- 常驻 Hermes 接口选择与许可结论。
- `hermes-bridge` adapter 及最小协议测试。
- 会话、事件、interrupt 和 idle-GC 验证记录。
- CLI 与常驻路径的 cold/warm 延迟对比报告。
- Skill/产品资产部署前后只读摘要对比。
- 切换与回滚说明。

本任务书只定义后续实施范围。创建本文档本身不授权修改、部署或重启 Hermes
服务器，也不授权改动任何 Skill、公司介绍或产品执行链路。
