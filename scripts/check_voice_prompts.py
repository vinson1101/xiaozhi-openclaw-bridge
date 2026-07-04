from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from xiaozhi_openclaw_bridge.voice import FIXED_VOICE_PROMPTS, fixed_voice_prompt  # noqa: E402


def main() -> None:
    required = {"wake", "listen", "confirm", "interrupt", "error", "setup_start", "setup_done"}
    ids = {prompt.prompt_id for prompt in FIXED_VOICE_PROMPTS}
    cache_keys = {prompt.cache_key for prompt in FIXED_VOICE_PROMPTS}
    assert required <= ids
    assert len(ids) == len(FIXED_VOICE_PROMPTS)
    assert len(cache_keys) == len(FIXED_VOICE_PROMPTS)
    assert fixed_voice_prompt("wake").text == "你好，我是小元。"
    for prompt in FIXED_VOICE_PROMPTS:
        assert 1 <= len(prompt.text) <= 20
        assert prompt.eye_state in {"listening", "thinking", "speaking", "error"}
        assert prompt.cache_key.startswith("fixed/xiaoyuan/")
    print("check_voice_prompts ok")


if __name__ == "__main__":
    main()
