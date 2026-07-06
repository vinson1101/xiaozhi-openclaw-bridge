from __future__ import annotations

import base64
import io
import json
import os
import socket
import ssl
import struct
import tempfile
import urllib.error
import urllib.request
import wave
from dataclasses import dataclass
from typing import Any
from urllib.parse import urlsplit
from uuid import uuid4


@dataclass(frozen=True)
class AsrRequest:
    audio: bytes
    sample_rate: int
    channels: int
    language: str = "zh"
    hints: tuple[str, ...] = ()
    audio_format: str = "pcm16"
    frame_duration_ms: int = 20
    audio_frames: tuple[bytes, ...] = ()


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


class OpenAIAsrProvider:
    provider = "openai"

    def transcribe(self, request: AsrRequest) -> AsrResponse:
        if not request.audio:
            return AsrResponse("error", "", "empty audio")
        if not os.environ.get("OPENAI_API_KEY"):
            return AsrResponse("error", "", "OPENAI_API_KEY is required for openai ASR")

        try:
            audio, suffix = _as_supported_audio_file(request)
        except ValueError as exc:
            return AsrResponse("error", "", str(exc))

        try:
            from openai import OpenAI
        except ImportError:
            return AsrResponse("error", "", "openai package is not installed")

        model = os.environ.get("XOB_OPENAI_ASR_MODEL", "gpt-4o-mini-transcribe").strip()
        prompt = os.environ.get("XOB_OPENAI_ASR_PROMPT", "").strip()
        if not prompt and request.hints:
            prompt = " ".join(request.hints)

        try:
            with tempfile.NamedTemporaryFile(suffix=suffix) as tmp:
                tmp.write(audio)
                tmp.flush()
                tmp.seek(0)
                kwargs: dict[str, Any] = {
                    "model": model,
                    "file": tmp,
                    "response_format": "text",
                }
                if prompt:
                    kwargs["prompt"] = prompt
                result = OpenAI().audio.transcriptions.create(**kwargs)
        except Exception as exc:  # provider errors should not crash the device loop
            return AsrResponse("error", "", f"openai ASR failed: {_truncate(str(exc), 300)}")

        text = result if isinstance(result, str) else str(getattr(result, "text", "") or result)
        text = text.strip()
        return AsrResponse(
            "done" if text else "error",
            text,
            f"openai asr {len(request.audio)} bytes format={request.audio_format} model={model}",
        )


class BailianFunAsrFlashProvider:
    provider = "bailian_fun_flash"

    def transcribe(self, request: AsrRequest) -> AsrResponse:
        if not request.audio and not request.audio_frames:
            return AsrResponse("error", "", "empty audio")

        api_key = _first_env("XOB_BAILIAN_API_KEY", "XOB_DASHSCOPE_API_KEY", "DASHSCOPE_API_KEY")
        if not api_key:
            return AsrResponse(
                "error",
                "",
                "XOB_BAILIAN_API_KEY or DASHSCOPE_API_KEY is required for bailian_fun_flash ASR",
            )

        model = os.environ.get("XOB_BAILIAN_ASR_MODEL", "fun-asr-flash-2026-06-15").strip()
        base_url = os.environ.get("XOB_BAILIAN_BASE_URL", "https://dashscope.aliyuncs.com/api/v1").strip()
        endpoint = _bailian_endpoint(base_url)

        try:
            audio, suffix = _as_supported_audio_file(request)
            data_uri, format_name = _bailian_audio_data(audio, suffix)
        except ValueError as exc:
            return AsrResponse("error", "", str(exc))

        messages: list[dict[str, Any]] = []
        if request.hints:
            messages.append(
                {
                    "role": "user",
                    "content": [{"type": "input_text", "text": " ".join(request.hints)[:400]}],
                }
            )
        messages.append(
            {
                "role": "user",
                "content": [{"type": "input_audio", "input_audio": {"data": data_uri}}],
            }
        )
        payload = {
            "model": model,
            "input": {"messages": messages},
            "parameters": {"format": format_name, "sample_rate": str(request.sample_rate)},
        }

        try:
            http_request = urllib.request.Request(
                endpoint,
                data=json.dumps(payload).encode("utf-8"),
                headers={
                    "Authorization": f"Bearer {api_key}",
                    "Content-Type": "application/json",
                    "X-DashScope-SSE": "disable",
                },
                method="POST",
            )
            with urllib.request.urlopen(
                http_request,
                timeout=_int_env("XOB_BAILIAN_ASR_TIMEOUT_SECONDS", 60),
            ) as response:
                raw = response.read()
        except urllib.error.HTTPError as exc:
            body = exc.read().decode("utf-8", errors="replace")
            return AsrResponse("error", "", f"bailian ASR HTTP {exc.code}: {_truncate(body, 300)}")
        except Exception as exc:  # provider errors should not crash the device loop
            return AsrResponse("error", "", f"bailian ASR failed: {_truncate(str(exc), 300)}")

        try:
            result = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            return AsrResponse("error", "", f"bailian ASR returned invalid JSON: {_truncate(str(exc), 160)}")

        text = _bailian_text_from_response(result)
        summary = f"bailian fun-asr-flash {len(audio)} bytes format={format_name} model={model}"
        usage = result.get("usage") if isinstance(result, dict) else None
        if isinstance(usage, dict) and usage.get("duration") is not None:
            summary += f" duration={usage['duration']}s"
        return AsrResponse("done" if text else "error", text, summary)


class BailianParaformerRealtimeProvider:
    provider = "bailian_paraformer_realtime"

    def transcribe(self, request: AsrRequest) -> AsrResponse:
        if not request.audio and not request.audio_frames:
            return AsrResponse("error", "", "empty audio")

        api_key = _first_env("XOB_BAILIAN_API_KEY", "XOB_DASHSCOPE_API_KEY", "DASHSCOPE_API_KEY")
        if not api_key:
            return AsrResponse(
                "error",
                "",
                "XOB_BAILIAN_API_KEY or DASHSCOPE_API_KEY is required for bailian_paraformer_realtime ASR",
            )

        model = os.environ.get("XOB_BAILIAN_PARA_ASR_MODEL", "paraformer-realtime-v2").strip()
        base_url = os.environ.get("XOB_BAILIAN_BASE_URL", "https://dashscope.aliyuncs.com/api/v1").strip()

        try:
            audio, format_name = _paraformer_realtime_audio(request)
            text, duration = _run_paraformer_realtime(
                api_key=api_key,
                base_url=base_url,
                model=model,
                audio=audio,
                audio_format=format_name,
                sample_rate=request.sample_rate,
                timeout=_int_env("XOB_BAILIAN_ASR_TIMEOUT_SECONDS", 60),
            )
        except ValueError as exc:
            return AsrResponse("error", "", str(exc))
        except Exception as exc:  # provider errors should not crash the device loop
            return AsrResponse("error", "", f"bailian paraformer ASR failed: {_truncate(str(exc), 300)}")

        summary = f"bailian paraformer realtime {len(audio)} bytes format={format_name} model={model}"
        if duration is not None:
            summary += f" duration={duration}s"
        return AsrResponse("done" if text else "error", text, summary)


def asr_provider_for(provider: str | None = None) -> Any:
    selected = (provider or os.environ.get("XOB_ASR_PROVIDER") or "fake").strip() or "fake"
    if selected == "fake":
        return FakeAsrProvider()
    if selected == "openai":
        return OpenAIAsrProvider()
    if selected in {"bailian_fun_flash", "fun_asr_flash", "dashscope_fun_flash"}:
        return BailianFunAsrFlashProvider()
    if selected in {"bailian_paraformer_realtime", "paraformer_realtime", "dashscope_paraformer_realtime"}:
        return BailianParaformerRealtimeProvider()
    raise ValueError(f"unsupported ASR provider: {selected}")


def _as_supported_audio_file(request: AsrRequest) -> tuple[bytes, str]:
    audio_format = request.audio_format.strip().lower()
    if audio_format == "wav":
        return request.audio, ".wav"
    if audio_format in {"pcm16", "pcm_s16le"}:
        return _wav_from_pcm16(request.audio, request.sample_rate, request.channels), ".wav"
    if audio_format in {"opus_frames", "opus"}:
        frames = request.audio_frames or _split_fixed_opus_frames(request.audio)
        if not frames:
            raise ValueError("opus audio frames are required")
        return _ogg_opus_from_frames(
            frames,
            sample_rate=request.sample_rate,
            channels=request.channels,
            frame_duration_ms=request.frame_duration_ms,
        ), ".ogg"
    raise ValueError(f"unsupported ASR audio format: {request.audio_format}")


def _wav_from_pcm16(audio: bytes, sample_rate: int, channels: int) -> bytes:
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        wav.writeframes(audio)
    return output.getvalue()


def _split_fixed_opus_frames(audio: bytes) -> tuple[bytes, ...]:
    frame_bytes = _int_env("XOB_ASR_OPUS_FRAME_BYTES", 40)
    if frame_bytes <= 0 or len(audio) % frame_bytes != 0:
        raise ValueError("opus frame boundaries are unavailable")
    return tuple(audio[index : index + frame_bytes] for index in range(0, len(audio), frame_bytes))


def _paraformer_realtime_audio(request: AsrRequest) -> tuple[bytes, str]:
    audio_format = request.audio_format.strip().lower()
    if audio_format == "wav":
        return request.audio, "wav"
    if audio_format in {"pcm16", "pcm_s16le"}:
        return request.audio, "pcm"
    if audio_format in {"opus_frames", "opus"}:
        if request.audio.startswith(b"OggS"):
            return request.audio, "opus"
        frames = request.audio_frames or _split_fixed_opus_frames(request.audio)
        if not frames:
            raise ValueError("opus audio frames are required")
        return (
            _ogg_opus_from_frames(
                frames,
                sample_rate=request.sample_rate,
                channels=request.channels,
                frame_duration_ms=request.frame_duration_ms,
            ),
            "opus",
        )
    raise ValueError(f"unsupported Paraformer realtime audio format: {request.audio_format}")


def _bailian_endpoint(base_url: str) -> str:
    endpoint = base_url.rstrip("/")
    if endpoint.endswith("/services/aigc/multimodal-generation/generation"):
        return endpoint
    return endpoint + "/services/aigc/multimodal-generation/generation"


def _bailian_audio_data(audio: bytes, suffix: str) -> tuple[str, str]:
    if suffix == ".wav":
        mime_type = "audio/wav"
        format_name = "wav"
    elif suffix == ".ogg":
        mime_type = "audio/ogg"
        format_name = "ogg"
    else:
        raise ValueError(f"unsupported bailian ASR audio suffix: {suffix}")
    encoded = base64.b64encode(audio).decode("ascii")
    return f"data:{mime_type};base64,{encoded}", format_name


def _bailian_text_from_response(response: dict[str, Any]) -> str:
    output = response.get("output")
    if isinstance(output, dict):
        text = output.get("text")
        if isinstance(text, str) and text.strip():
            return text.strip()
        sentence = output.get("sentence")
        if isinstance(sentence, dict):
            text = sentence.get("text")
            if isinstance(text, str):
                return text.strip()
    text = response.get("text")
    if isinstance(text, str) and text.strip():
        return text.strip()
    sentence = response.get("sentence")
    if isinstance(sentence, dict):
        text = sentence.get("text")
        if isinstance(text, str):
            return text.strip()
    return ""


def _run_paraformer_realtime(
    api_key: str,
    base_url: str,
    model: str,
    audio: bytes,
    audio_format: str,
    sample_rate: int,
    timeout: int,
) -> tuple[str, int | None]:
    host, path = _bailian_ws_host_path(base_url)
    task_id = str(uuid4())
    sock = _ws_connect(host, path, api_key, timeout)
    try:
        run_task = {
            "header": {"action": "run-task", "task_id": task_id, "streaming": "duplex"},
            "payload": {
                "task_group": "audio",
                "task": "asr",
                "function": "recognition",
                "model": model,
                "parameters": {
                    "format": audio_format,
                    "sample_rate": sample_rate,
                    "disfluency_removal_enabled": False,
                    "punctuation_prediction_enabled": True,
                    "semantic_punctuation_enabled": False,
                    "max_sentence_silence": _int_env("XOB_BAILIAN_PARA_MAX_SENTENCE_SILENCE_MS", 800),
                },
                "input": {},
            },
        }
        if os.environ.get("XOB_BAILIAN_PARA_LANGUAGE_HINTS", "zh,en").strip():
            run_task["payload"]["parameters"]["language_hints"] = [
                item.strip()
                for item in os.environ.get("XOB_BAILIAN_PARA_LANGUAGE_HINTS", "zh,en").split(",")
                if item.strip()
            ]
        _ws_send(sock, 1, json.dumps(run_task).encode())
        _wait_ws_event(sock, "task-started")

        chunk_bytes = _int_env("XOB_BAILIAN_PARA_AUDIO_CHUNK_BYTES", 3200)
        for offset in range(0, len(audio), chunk_bytes):
            _ws_send(sock, 2, audio[offset : offset + chunk_bytes])

        _ws_send(
            sock,
            1,
            json.dumps(
                {
                    "header": {"action": "finish-task", "task_id": task_id, "streaming": "duplex"},
                    "payload": {"input": {}},
                }
            ).encode(),
        )
        return _read_paraformer_result(sock)
    finally:
        try:
            sock.close()
        except OSError:
            pass


def _bailian_ws_host_path(base_url: str) -> tuple[str, str]:
    explicit = os.environ.get("XOB_BAILIAN_ASR_WS_URL", "").strip()
    parsed = urlsplit(explicit or base_url)
    host = parsed.netloc or parsed.path
    if not host:
        raise ValueError("Bailian WebSocket host is empty")
    return host, "/api-ws/v1/inference"


def _ws_connect(host: str, path: str, api_key: str, timeout: int) -> ssl.SSLSocket:
    raw = socket.create_connection((host, 443), timeout=timeout)
    sock = ssl.create_default_context().wrap_socket(raw, server_hostname=host)
    sock.settimeout(timeout)
    key = base64.b64encode(os.urandom(16)).decode()
    request = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {host}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        f"Authorization: Bearer {api_key}\r\n"
        "user-agent: xiaozhi-openclaw-bridge\r\n"
        "\r\n"
    ).encode()
    sock.sendall(request)
    response = _read_until(sock, b"\r\n\r\n")
    if b" 101 " not in response.split(b"\r\n", 1)[0]:
        raise ValueError(_truncate(response.decode("utf-8", errors="replace"), 300))
    return sock


def _wait_ws_event(sock: ssl.SSLSocket, expected: str) -> None:
    while True:
        opcode, payload = _ws_read(sock)
        if opcode != 1:
            continue
        message = _json_loads(payload)
        header = message.get("header") if isinstance(message.get("header"), dict) else {}
        event = header.get("event")
        if event == expected:
            return
        if event == "task-failed":
            raise ValueError(header.get("error_message") or header.get("error_code") or "Paraformer task failed")


def _read_paraformer_result(sock: ssl.SSLSocket) -> tuple[str, int | None]:
    final_texts: list[str] = []
    latest_text = ""
    duration: int | None = None
    while True:
        opcode, payload = _ws_read(sock)
        if opcode != 1:
            continue
        message = _json_loads(payload)
        header = message.get("header") if isinstance(message.get("header"), dict) else {}
        event = header.get("event")
        if event == "task-failed":
            raise ValueError(header.get("error_message") or header.get("error_code") or "Paraformer task failed")
        payload_obj = message.get("payload") if isinstance(message.get("payload"), dict) else {}
        output = payload_obj.get("output") if isinstance(payload_obj.get("output"), dict) else {}
        sentence = output.get("sentence") if isinstance(output.get("sentence"), dict) else {}
        text = sentence.get("text")
        if isinstance(text, str) and text.strip():
            latest_text = text.strip()
            if sentence.get("sentence_end") is True:
                final_texts.append(latest_text)
        usage = payload_obj.get("usage") if isinstance(payload_obj.get("usage"), dict) else {}
        if isinstance(usage.get("duration"), int):
            duration = usage["duration"]
        if event == "task-finished":
            break
    return ("".join(final_texts).strip() or latest_text, duration)


def _json_loads(payload: bytes) -> dict[str, Any]:
    try:
        decoded = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid WebSocket JSON from Bailian: {_truncate(str(exc), 160)}") from exc
    if not isinstance(decoded, dict):
        raise ValueError("invalid WebSocket JSON from Bailian")
    return decoded


def _read_until(sock: socket.socket, marker: bytes) -> bytes:
    data = b""
    while marker not in data:
        chunk = sock.recv(4096)
        if not chunk:
            break
        data += chunk
    return data


def _ws_send(sock: socket.socket, opcode: int, payload: bytes) -> None:
    mask = os.urandom(4)
    length = len(payload)
    if length < 126:
        header = bytes([0x80 | opcode, 0x80 | length])
    elif length < 65536:
        header = bytes([0x80 | opcode, 0x80 | 126]) + length.to_bytes(2, "big")
    else:
        header = bytes([0x80 | opcode, 0x80 | 127]) + length.to_bytes(8, "big")
    masked = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
    sock.sendall(header + mask + masked)


def _ws_read(sock: socket.socket) -> tuple[int, bytes]:
    header = _recv_exact(sock, 2)
    first, second = header
    opcode = first & 0x0F
    length = second & 0x7F
    if length == 126:
        length = int.from_bytes(_recv_exact(sock, 2), "big")
    elif length == 127:
        length = int.from_bytes(_recv_exact(sock, 8), "big")
    mask = _recv_exact(sock, 4) if second & 0x80 else b""
    payload = _recv_exact(sock, length)
    if mask:
        payload = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
    return opcode, payload


def _recv_exact(sock: socket.socket, size: int) -> bytes:
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise OSError("truncated websocket frame")
        data += chunk
    return data


def _ogg_opus_from_frames(
    frames: tuple[bytes, ...],
    sample_rate: int,
    channels: int,
    frame_duration_ms: int,
) -> bytes:
    if channels != 1:
        raise ValueError("only mono Opus input is supported")
    serial = 0x584F4201
    sequence = 0
    output = bytearray()
    opus_head = b"OpusHead" + struct.pack("<BBHIhB", 1, channels, 312, sample_rate, 0, 0)
    opus_tags = b"OpusTags" + struct.pack("<I", 3) + b"xob" + struct.pack("<I", 0)
    output += _ogg_page(serial, sequence, 0x02, 0, [opus_head])
    sequence += 1
    output += _ogg_page(serial, sequence, 0x00, 0, [opus_tags])
    sequence += 1

    samples_per_frame = max(1, int(48000 * frame_duration_ms / 1000))
    granule = 0
    for index, frame in enumerate(frames):
        granule += samples_per_frame
        header_type = 0x04 if index == len(frames) - 1 else 0x00
        output += _ogg_page(serial, sequence, header_type, granule, [frame])
        sequence += 1
    return bytes(output)


def _ogg_page(serial: int, sequence: int, header_type: int, granule: int, packets: list[bytes]) -> bytes:
    segments: list[int] = []
    body = bytearray()
    for packet in packets:
        remaining = len(packet)
        offset = 0
        while remaining >= 255:
            segments.append(255)
            body += packet[offset : offset + 255]
            offset += 255
            remaining -= 255
        segments.append(remaining)
        body += packet[offset:]

    header = bytearray(b"OggS")
    header += bytes([0, header_type])
    header += struct.pack("<qIIIB", granule, serial, sequence, 0, len(segments))
    header += bytes(segments)
    page = bytes(header) + bytes(body)
    checksum = _ogg_crc(page)
    return page[:22] + struct.pack("<I", checksum) + page[26:]


def _ogg_crc(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ _OGG_CRC_TABLE[((crc >> 24) & 0xFF) ^ byte]
    return crc


def _make_ogg_crc_table() -> tuple[int, ...]:
    table: list[int] = []
    for value in range(256):
        register = value << 24
        for _ in range(8):
            if register & 0x80000000:
                register = ((register << 1) ^ 0x04C11DB7) & 0xFFFFFFFF
            else:
                register = (register << 1) & 0xFFFFFFFF
        table.append(register)
    return tuple(table)


def _int_env(name: str, default: int) -> int:
    raw = os.environ.get(name, "").strip()
    if not raw:
        return default
    try:
        value = int(raw)
    except ValueError:
        return default
    return value if value > 0 else default


def _first_env(*names: str) -> str:
    for name in names:
        value = os.environ.get(name, "").strip()
        if value:
            return value
    return ""


def _truncate(text: str, limit: int) -> str:
    return text if len(text) <= limit else text[: limit - 3] + "..."


_OGG_CRC_TABLE = _make_ogg_crc_table()
