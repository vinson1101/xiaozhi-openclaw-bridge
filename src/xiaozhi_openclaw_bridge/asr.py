from __future__ import annotations

import os
from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True)
class AsrRequest:
    audio: bytes
    sample_rate: int
    channels: int
    language: str = "zh"
    hints: tuple[str, ...] = ()


@dataclass(frozen=True)
class AsrResponse:
    status: str
    text: str
    summary: str


class FakeAsrProvider:
    provider = "fake"

    def transcribe(self, request: AsrRequest) -> AsrResponse:
        if not request.audio:
            return AsrResponse("error", "", "empty audio")
        text = os.environ.get("XOB_FAKE_ASR_TEXT", "你好，检查链路").strip() or "你好，检查链路"
        return AsrResponse("done", text, f"fake asr {len(request.audio)} bytes")


def asr_provider_for(provider: str | None = None) -> Any:
    selected = (provider or os.environ.get("XOB_ASR_PROVIDER") or "fake").strip() or "fake"
    if selected == "fake":
        return FakeAsrProvider()
    raise ValueError(f"unsupported ASR provider: {selected}")
