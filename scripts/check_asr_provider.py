from __future__ import annotations

import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from xiaozhi_openclaw_bridge.asr import (  # noqa: E402
    AsrRequest,
    _bailian_audio_data,
    _bailian_text_from_response,
    _ogg_opus_from_frames,
    _paraformer_realtime_audio,
    asr_provider_for,
)


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

    old_key = os.environ.pop("OPENAI_API_KEY", None)
    openai = asr_provider_for("openai")
    response = openai.transcribe(
        AsrRequest(
            audio=b"\0" * 40,
            sample_rate=16000,
            channels=1,
            audio_format="opus_frames",
            audio_frames=(b"\0" * 40,),
        )
    )
    assert response.status == "error"
    assert response.summary == "OPENAI_API_KEY is required for openai ASR"
    if old_key is not None:
        os.environ["OPENAI_API_KEY"] = old_key

    old_bailian_key = os.environ.pop("XOB_BAILIAN_API_KEY", None)
    old_xob_dashscope_key = os.environ.pop("XOB_DASHSCOPE_API_KEY", None)
    old_dashscope_key = os.environ.pop("DASHSCOPE_API_KEY", None)
    bailian = asr_provider_for("bailian_fun_flash")
    response = bailian.transcribe(AsrRequest(audio=b"\0" * 320, sample_rate=16000, channels=1))
    assert response.status == "error"
    assert "DASHSCOPE_API_KEY is required" in response.summary
    paraformer = asr_provider_for("paraformer_realtime")
    response = paraformer.transcribe(AsrRequest(audio=b"\0" * 320, sample_rate=16000, channels=1))
    assert response.status == "error"
    assert "DASHSCOPE_API_KEY is required" in response.summary
    if old_bailian_key is not None:
        os.environ["XOB_BAILIAN_API_KEY"] = old_bailian_key
    if old_xob_dashscope_key is not None:
        os.environ["XOB_DASHSCOPE_API_KEY"] = old_xob_dashscope_key
    if old_dashscope_key is not None:
        os.environ["DASHSCOPE_API_KEY"] = old_dashscope_key

    data_uri, format_name = _bailian_audio_data(b"RIFFtest", ".wav")
    assert data_uri.startswith("data:audio/wav;base64,")
    assert format_name == "wav"
    data_uri, format_name = _bailian_audio_data(b"OggStest", ".ogg")
    assert data_uri.startswith("data:audio/ogg;base64,")
    assert format_name == "ogg"
    assert _bailian_text_from_response({"output": {"text": "你好，小元"}}) == "你好，小元"
    assert _bailian_text_from_response({"output": {"sentence": {"text": "你好，小智"}}}) == "你好，小智"
    assert _bailian_text_from_response({"sentence": {"text": "测试成功。"}}) == "测试成功。"
    audio, format_name = _paraformer_realtime_audio(
        AsrRequest(audio=b"\1\2" * 160, sample_rate=16000, channels=1, audio_format="pcm16")
    )
    assert audio == b"\1\2" * 160
    assert format_name == "pcm"

    ogg = _ogg_opus_from_frames((b"\0" * 40, b"\0" * 40), sample_rate=16000, channels=1, frame_duration_ms=20)
    assert ogg.startswith(b"OggS")
    assert b"OpusHead" in ogg
    assert b"OpusTags" in ogg
    audio, format_name = _paraformer_realtime_audio(
        AsrRequest(
            audio=b"\0" * 80,
            sample_rate=16000,
            channels=1,
            audio_format="opus_frames",
            audio_frames=(b"\0" * 40, b"\0" * 40),
        )
    )
    assert audio.startswith(b"OggS")
    assert format_name == "opus"
    print("check_asr_provider ok")


if __name__ == "__main__":
    main()
