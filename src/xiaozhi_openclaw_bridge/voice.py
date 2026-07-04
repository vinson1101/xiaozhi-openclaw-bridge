from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class FixedVoicePrompt:
    prompt_id: str
    text: str
    eye_state: str
    cache_key: str


FIXED_VOICE_PROMPTS: tuple[FixedVoicePrompt, ...] = (
    FixedVoicePrompt("wake", "你好，我是小元。", "speaking", "fixed/xiaoyuan/wake"),
    FixedVoicePrompt("listen", "我在，正在听。", "listening", "fixed/xiaoyuan/listen"),
    FixedVoicePrompt("confirm", "收到，我来处理。", "thinking", "fixed/xiaoyuan/confirm"),
    FixedVoicePrompt("interrupt", "已打断，重新听你说。", "listening", "fixed/xiaoyuan/interrupt"),
    FixedVoicePrompt("error", "刚才出错了，请再说一次。", "error", "fixed/xiaoyuan/error"),
    FixedVoicePrompt("setup_start", "进入配置模式。", "thinking", "fixed/xiaoyuan/setup_start"),
    FixedVoicePrompt("setup_done", "配置好了。", "speaking", "fixed/xiaoyuan/setup_done"),
)


def fixed_voice_prompt(prompt_id: str) -> FixedVoicePrompt:
    for prompt in FIXED_VOICE_PROMPTS:
        if prompt.prompt_id == prompt_id:
            return prompt
    raise KeyError(prompt_id)
