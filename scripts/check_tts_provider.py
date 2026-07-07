from __future__ import annotations

import sys
import base64
import os
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from xiaozhi_openclaw_bridge.server import _opus_frames_from_audio, _tts_audio_frames  # noqa: E402
from xiaozhi_openclaw_bridge.tts import BailianTtsProvider, MiniMaxTtsProvider, TtsRequest, synthesize_fixed_prompt, tts_provider_for  # noqa: E402


def main() -> None:
    provider = tts_provider_for("fake")
    dynamic = provider.synthesize(TtsRequest(text="小元测试", cache_key="dynamic/test"))
    assert dynamic.status == "done"
    assert dynamic.content_type == "audio/wav"
    assert dynamic.audio.startswith(b"RIFF")
    assert any(dynamic.audio[44:])
    assert dynamic.cache_key == "dynamic/test"
    if shutil.which("ffmpeg"):
        opus_frames = _opus_frames_from_audio(dynamic.audio, dynamic.content_type)
        assert len(opus_frames) >= 1
        assert not opus_frames[0].startswith((b"OpusHead", b"OpusTags", b"OggS"))
        os.environ["XOB_WS_TTS_AUDIO_CODEC"] = "opus"
        try:
            wire_frames = _tts_audio_frames(dynamic.audio, dynamic.content_type)
        finally:
            os.environ.pop("XOB_WS_TTS_AUDIO_CODEC", None)
        assert wire_frames == opus_frames

    fixed = synthesize_fixed_prompt("interrupt", provider)
    assert fixed.status == "done"
    assert fixed.audio.startswith(b"RIFF")
    assert fixed.cache_key == "fixed/xiaoyuan/interrupt"

    empty = provider.synthesize(TtsRequest(text=""))
    assert empty.status == "error"
    assert empty.summary == "empty text"

    minimax_without_key = MiniMaxTtsProvider(api_key="")
    missing_key = minimax_without_key.synthesize(TtsRequest(text="小元测试"))
    assert missing_key.status == "error"
    assert missing_key.summary == "MINIMAX_API_KEY is required for minimax TTS"

    captured_payload = {}

    def fake_post(url, api_key, payload, timeout):
        captured_payload.update({"url": url, "api_key": api_key, "payload": payload, "timeout": timeout})
        return {"base_resp": {"status_code": 0}, "data": {"audio": dynamic.audio.hex()}}

    minimax = MiniMaxTtsProvider(api_key="test-key", voice_id="Chinese (Mandarin)_Gentleman", http_post=fake_post)
    realish = minimax.synthesize(TtsRequest(text="小元测试", cache_key="dynamic/minimax"))
    assert realish.status == "done"
    assert realish.content_type == "audio/wav"
    assert realish.audio.startswith(b"RIFF")
    assert realish.cache_key == "dynamic/minimax"
    assert captured_payload["api_key"] == "test-key"
    assert captured_payload["payload"]["model"] == "speech-2.8-turbo"
    assert captured_payload["payload"]["voice_setting"]["voice_id"] == "Chinese (Mandarin)_Gentleman"
    assert captured_payload["payload"]["audio_setting"]["sample_rate"] == 16000
    assert captured_payload["payload"]["audio_setting"]["format"] == "wav"

    minimax_error = MiniMaxTtsProvider(
        api_key="test-key",
        http_post=lambda *_: {"base_resp": {"status_code": 1001, "status_msg": "bad voice"}},
    )
    failed = minimax_error.synthesize(TtsRequest(text="小元测试"))
    assert failed.status == "error"
    assert failed.summary == "minimax status 1001: bad voice"

    captured_stream_payload = {}

    def fake_stream(url, api_key, payload, timeout):
        captured_stream_payload.update({"url": url, "api_key": api_key, "payload": payload, "timeout": timeout})
        yield {"output": {"audio": {"data": base64.b64encode(dynamic.audio[:100]).decode()}}}
        yield {"output": {"audio": {"data": base64.b64encode(dynamic.audio[100:200]).decode()}}}

    bailian = BailianTtsProvider(api_key="test-key", voice_id="longanyang", http_stream=fake_stream)
    chunks = list(bailian.stream_audio(TtsRequest(text="小元测试")))
    assert chunks
    assert not chunks[0].startswith(b"RIFF")
    assert captured_stream_payload["api_key"] == "test-key"
    assert captured_stream_payload["url"].endswith("/services/audio/tts/SpeechSynthesizer")
    assert captured_stream_payload["payload"]["model"] == "cosyvoice-v3-flash"
    assert captured_stream_payload["payload"]["input"]["voice"] == "longanyang"
    assert captured_stream_payload["payload"]["input"]["sample_rate"] == 16000

    print("check_tts_provider ok")


if __name__ == "__main__":
    main()
