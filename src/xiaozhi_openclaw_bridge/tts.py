from __future__ import annotations

import io
import os
import wave
from dataclasses import dataclass
from typing import Any

from xiaozhi_openclaw_bridge.voice import fixed_voice_prompt


@dataclass(frozen=True)
class TtsRequest:
    text: str
    voice: str = "xiaoyuan"
    cache_key: str = ""


@dataclass(frozen=True)
class TtsResponse:
    status: str
    audio: bytes
    content_type: str
    cache_key: str
    summary: str


class FakeTtsProvider:
    provider = "fake"

    def synthesize(self, request: TtsRequest) -> TtsResponse:
        if not request.text.strip():
            return TtsResponse("error", b"", "audio/wav", request.cache_key, "empty text")
        return TtsResponse(
            "done",
            _silent_wav(),
            "audio/wav",
            request.cache_key,
            f"fake tts {len(request.text)} chars",
        )


def tts_provider_for(provider: str | None = None) -> Any:
    selected = (provider or os.environ.get("XOB_TTS_PROVIDER") or "fake").strip() or "fake"
    if selected == "fake":
        return FakeTtsProvider()
    raise ValueError(f"unsupported TTS provider: {selected}")


def synthesize_fixed_prompt(prompt_id: str, provider: Any | None = None) -> TtsResponse:
    prompt = fixed_voice_prompt(prompt_id)
    return (provider or tts_provider_for()).synthesize(
        TtsRequest(text=prompt.text, voice="xiaoyuan", cache_key=prompt.cache_key)
    )


def _silent_wav() -> bytes:
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(16000)
        wav.writeframes(b"\0\0" * 1600)
    return buffer.getvalue()
