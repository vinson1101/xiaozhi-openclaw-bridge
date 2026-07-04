from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from xiaozhi_openclaw_bridge.tts import TtsRequest, synthesize_fixed_prompt, tts_provider_for  # noqa: E402


def main() -> None:
    provider = tts_provider_for("fake")
    dynamic = provider.synthesize(TtsRequest(text="小元测试", cache_key="dynamic/test"))
    assert dynamic.status == "done"
    assert dynamic.content_type == "audio/wav"
    assert dynamic.audio.startswith(b"RIFF")
    assert dynamic.cache_key == "dynamic/test"

    fixed = synthesize_fixed_prompt("interrupt", provider)
    assert fixed.status == "done"
    assert fixed.audio.startswith(b"RIFF")
    assert fixed.cache_key == "fixed/xiaoyuan/interrupt"

    empty = provider.synthesize(TtsRequest(text=""))
    assert empty.status == "error"
    assert empty.summary == "empty text"
    print("check_tts_provider ok")


if __name__ == "__main__":
    main()
