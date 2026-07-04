from __future__ import annotations

import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from xiaozhi_openclaw_bridge.asr import AsrRequest, asr_provider_for  # noqa: E402


def main() -> None:
    provider = asr_provider_for("fake")
    response = provider.transcribe(AsrRequest(audio=b"\0" * 320, sample_rate=16000, channels=1))
    assert response.status == "done"
    assert response.text == "你好，检查链路"
    assert "320 bytes" in response.summary

    empty = provider.transcribe(AsrRequest(audio=b"", sample_rate=16000, channels=1))
    assert empty.status == "error"
    assert empty.summary == "empty audio"

    os.environ["XOB_FAKE_ASR_TEXT"] = "小元测试"
    assert asr_provider_for().transcribe(AsrRequest(audio=b"1", sample_rate=16000, channels=1)).text == "小元测试"
    os.environ.pop("XOB_FAKE_ASR_TEXT")
    print("check_asr_provider ok")


if __name__ == "__main__":
    main()
