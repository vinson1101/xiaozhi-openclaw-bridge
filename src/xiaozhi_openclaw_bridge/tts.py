from __future__ import annotations

import io
import json
import math
import os
import struct
import base64
import urllib.error
import urllib.request
import wave
from dataclasses import dataclass
from typing import Any, Callable, Iterable

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
            _beep_wav(),
            "audio/wav",
            request.cache_key,
            f"fake tts {len(request.text)} chars",
        )


class MiniMaxTtsProvider:
    provider = "minimax"

    def __init__(
        self,
        api_key: str | None = None,
        endpoint: str | None = None,
        model: str | None = None,
        voice_id: str | None = None,
        http_post: Callable[[str, str, dict[str, Any], float], dict[str, Any]] | None = None,
    ) -> None:
        self.api_key = _first_config(api_key, "XOB_MINIMAX_API_KEY", "MINIMAX_API_KEY")
        self.endpoint = _first_config(endpoint, "XOB_MINIMAX_TTS_ENDPOINT", default="https://api.minimax.io/v1/t2a_v2")
        self.model = _first_config(model, "XOB_MINIMAX_TTS_MODEL", default="speech-2.8-turbo")
        self.voice_id = _first_config(voice_id, "XOB_MINIMAX_TTS_VOICE", default="Chinese (Mandarin)_Gentleman")
        self.http_post = http_post or _post_json

    def synthesize(self, request: TtsRequest) -> TtsResponse:
        text = request.text.strip()
        if not text:
            return TtsResponse("error", b"", "audio/wav", request.cache_key, "empty text")
        if not self.api_key:
            return TtsResponse("error", b"", "audio/wav", request.cache_key, "MINIMAX_API_KEY is required for minimax TTS")

        audio_format = os.environ.get("XOB_MINIMAX_TTS_FORMAT", "wav").strip().lower() or "wav"
        timeout = _float_env("XOB_MINIMAX_TTS_TIMEOUT", 30.0)
        payload = {
            "model": self.model,
            "text": text,
            "stream": False,
            "language_boost": "Chinese",
            "output_format": "hex",
            "voice_setting": {
                "voice_id": self.voice_id,
                "speed": _float_env("XOB_MINIMAX_TTS_SPEED", 1.0),
                "vol": _float_env("XOB_MINIMAX_TTS_VOL", 1.0),
                "pitch": _int_env("XOB_MINIMAX_TTS_PITCH", 0),
            },
            "audio_setting": {
                "sample_rate": _int_env("XOB_MINIMAX_TTS_SAMPLE_RATE", 16000),
                "bitrate": _int_env("XOB_MINIMAX_TTS_BITRATE", 128000),
                "format": audio_format,
                "channel": _int_env("XOB_MINIMAX_TTS_CHANNELS", 1),
            },
        }

        try:
            response = self.http_post(self.endpoint, self.api_key, payload, timeout)
            audio = _decode_minimax_audio(response)
        except ValueError as exc:
            return TtsResponse("error", b"", _content_type(audio_format), request.cache_key, str(exc))
        except Exception as exc:
            return TtsResponse("error", b"", _content_type(audio_format), request.cache_key, f"minimax TTS failed: {_truncate(str(exc), 300)}")

        return TtsResponse(
            "done",
            audio,
            _content_type(audio_format),
            request.cache_key,
            f"minimax tts {len(text)} chars model={self.model} voice={self.voice_id}",
        )


class BailianTtsProvider:
    provider = "bailian"

    def __init__(
        self,
        api_key: str | None = None,
        base_url: str | None = None,
        model: str | None = None,
        voice_id: str | None = None,
        http_stream: Callable[[str, str, dict[str, Any], float], Iterable[dict[str, Any]]] | None = None,
    ) -> None:
        self.api_key = _first_config(api_key, "XOB_BAILIAN_API_KEY", "XOB_DASHSCOPE_API_KEY", "DASHSCOPE_API_KEY")
        self.base_url = _first_config(base_url, "XOB_BAILIAN_BASE_URL", default="https://dashscope.aliyuncs.com/api/v1")
        self.model = _first_config(model, "XOB_BAILIAN_TTS_MODEL", default="cosyvoice-v3-flash")
        self.voice_id = _first_config(voice_id, "XOB_BAILIAN_TTS_VOICE", default="longanyang")
        self.http_stream = http_stream or _post_bailian_sse

    def synthesize(self, request: TtsRequest) -> TtsResponse:
        try:
            audio = b"".join(self.stream_audio(request))
        except ValueError as exc:
            return TtsResponse("error", b"", "audio/pcm", request.cache_key, str(exc))
        except Exception as exc:
            return TtsResponse("error", b"", "audio/pcm", request.cache_key, f"bailian TTS failed: {_truncate(str(exc), 300)}")
        return TtsResponse(
            "done" if audio else "error",
            audio,
            "audio/pcm",
            request.cache_key,
            f"bailian tts {len(request.text.strip())} chars model={self.model} voice={self.voice_id}",
        )

    def stream_audio(self, request: TtsRequest) -> Iterable[bytes]:
        text = request.text.strip()
        if not text:
            raise ValueError("empty text")
        if not self.api_key:
            raise ValueError("XOB_BAILIAN_API_KEY or DASHSCOPE_API_KEY is required for bailian TTS")

        payload_input: dict[str, Any] = {
            "text": text,
            "voice": self.voice_id,
            "format": "wav",
            "sample_rate": _int_env("XOB_BAILIAN_TTS_SAMPLE_RATE", 16000),
        }
        instruction = os.environ.get("XOB_BAILIAN_TTS_INSTRUCTION", "").strip()
        if instruction:
            payload_input["instruction"] = instruction
        payload = {"model": self.model, "input": payload_input}

        chunks = (
            _decode_bailian_audio_chunk(event)
            for event in self.http_stream(_bailian_tts_endpoint(self.base_url), self.api_key, payload, _float_env("XOB_BAILIAN_TTS_TIMEOUT", 30.0))
        )
        yield from _pcm_chunks_from_bailian_audio(chunk for chunk in chunks if chunk)


def tts_provider_for(provider: str | None = None) -> Any:
    selected = (provider or os.environ.get("XOB_TTS_PROVIDER") or "fake").strip() or "fake"
    if selected == "fake":
        return FakeTtsProvider()
    if selected == "minimax":
        return MiniMaxTtsProvider()
    if selected in {"bailian", "bailian_tts", "dashscope_tts"}:
        return BailianTtsProvider()
    raise ValueError(f"unsupported TTS provider: {selected}")


def synthesize_fixed_prompt(prompt_id: str, provider: Any | None = None) -> TtsResponse:
    prompt = fixed_voice_prompt(prompt_id)
    return (provider or tts_provider_for()).synthesize(
        TtsRequest(text=prompt.text, voice="xiaoyuan", cache_key=prompt.cache_key)
    )


def _beep_wav() -> bytes:
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(16000)
        samples = (
            int(12000 * math.sin(2 * math.pi * 880 * i / 16000))
            for i in range(16000)
        )
        wav.writeframes(b"".join(struct.pack("<h", sample) for sample in samples))
    return buffer.getvalue()


def _post_json(url: str, api_key: str, payload: dict[str, Any], timeout: float) -> dict[str, Any]:
    request = urllib.request.Request(
        url,
        data=json.dumps(payload, ensure_ascii=False).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            raw = response.read()
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise ValueError(f"minimax HTTP {exc.code}: {_truncate(detail, 200)}") from exc
    return json.loads(raw.decode("utf-8"))


def _post_bailian_sse(url: str, api_key: str, payload: dict[str, Any], timeout: float) -> Iterable[dict[str, Any]]:
    request = urllib.request.Request(
        url,
        data=json.dumps(payload, ensure_ascii=False).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
            "X-DashScope-SSE": "enable",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            for raw_line in response:
                line = raw_line.decode("utf-8", errors="replace").strip()
                if not line.startswith("data:"):
                    continue
                data = line[5:].strip()
                if data:
                    yield json.loads(data)
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise ValueError(f"bailian TTS HTTP {exc.code}: {_truncate(detail, 200)}") from exc


def _bailian_tts_endpoint(base_url: str) -> str:
    endpoint = base_url.rstrip("/")
    if endpoint.endswith("/services/audio/tts/SpeechSynthesizer"):
        return endpoint
    return endpoint + "/services/audio/tts/SpeechSynthesizer"


def _decode_bailian_audio_chunk(event: dict[str, Any]) -> bytes:
    output = event.get("output") if isinstance(event.get("output"), dict) else {}
    audio = output.get("audio") if isinstance(output.get("audio"), dict) else {}
    data = audio.get("data")
    if not isinstance(data, str) or not data.strip():
        return b""
    try:
        return base64.b64decode(data)
    except ValueError as exc:
        raise ValueError("bailian TTS returned invalid base64 audio") from exc


def _pcm_chunks_from_bailian_audio(chunks: Iterable[bytes]) -> Iterable[bytes]:
    started = False
    pending = b""
    for chunk in chunks:
        if started:
            yield chunk
            continue
        pending += chunk
        if pending.startswith(b"RIFF"):
            data_offset = _wav_data_offset(pending)
            if data_offset is None:
                continue
            started = True
            pcm = pending[data_offset:]
            pending = b""
            if pcm:
                yield pcm
        else:
            started = True
            yield pending
            pending = b""


def _wav_data_offset(wav_prefix: bytes) -> int | None:
    if len(wav_prefix) < 12 or wav_prefix[:4] != b"RIFF" or wav_prefix[8:12] != b"WAVE":
        return None
    offset = 12
    while offset + 8 <= len(wav_prefix):
        chunk_id = wav_prefix[offset : offset + 4]
        chunk_size = int.from_bytes(wav_prefix[offset + 4 : offset + 8], "little")
        chunk_start = offset + 8
        if chunk_id == b"data":
            return chunk_start
        offset = chunk_start + chunk_size + (chunk_size % 2)
    return None


def _decode_minimax_audio(response: dict[str, Any]) -> bytes:
    base_resp = response.get("base_resp") if isinstance(response.get("base_resp"), dict) else {}
    status_code = base_resp.get("status_code", response.get("status_code", 0))
    if status_code not in (0, "0", None):
        message = base_resp.get("status_msg") or base_resp.get("message") or response.get("message") or "unknown error"
        raise ValueError(f"minimax status {status_code}: {message}")

    data = response.get("data") if isinstance(response.get("data"), dict) else {}
    audio_hex = data.get("audio") or response.get("audio")
    if not isinstance(audio_hex, str) or not audio_hex.strip():
        raise ValueError("minimax response missing data.audio")
    try:
        return bytes.fromhex(audio_hex.strip())
    except ValueError as exc:
        raise ValueError("minimax response data.audio is not hex") from exc


def _content_type(audio_format: str) -> str:
    if audio_format == "pcm":
        return "audio/pcm"
    if audio_format == "mp3":
        return "audio/mpeg"
    if audio_format == "flac":
        return "audio/flac"
    return "audio/wav"


def _int_env(name: str, default: int) -> int:
    raw = os.environ.get(name, "").strip()
    if not raw:
        return default
    try:
        return int(raw)
    except ValueError:
        return default


def _float_env(name: str, default: float) -> float:
    raw = os.environ.get(name, "").strip()
    if not raw:
        return default
    try:
        return float(raw)
    except ValueError:
        return default


def _first_config(value: str | None, *env_names: str, default: str = "") -> str:
    if value is not None:
        return value.strip()
    for env_name in env_names:
        env_value = os.environ.get(env_name, "").strip()
        if env_value:
            return env_value
    return default


def _truncate(text: str, limit: int) -> str:
    return text if len(text) <= limit else text[: limit - 3] + "..."
