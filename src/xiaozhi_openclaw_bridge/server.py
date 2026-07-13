from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import queue
import re
import select
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Mapping
from urllib.parse import parse_qs, urlsplit
from uuid import uuid4

from xiaozhi_openclaw_bridge.adapters import AgentRequest, adapter_for
from xiaozhi_openclaw_bridge.asr import AsrRequest, asr_provider_for
from xiaozhi_openclaw_bridge.store import EventStore
from xiaozhi_openclaw_bridge.tts import TtsRequest, tts_provider_for

MAX_DEVICE_AUDIO_BYTES = 2 * 1024 * 1024
WEBSOCKET_TURN_TIMEOUT_SECONDS = 180
WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


class BridgeApplication:
    def __init__(
        self,
        db_path: str | Path,
        require_device_token: bool = False,
        allow_command_route: bool = True,
    ) -> None:
        self.require_device_token = require_device_token
        self.allow_command_route = allow_command_route
        self.store = EventStore(db_path)
        self.store.init()

    def handle(
        self,
        method: str,
        target: str,
        body: bytes | None,
        headers: Mapping[str, str] | None = None,
    ) -> tuple[int, dict[str, Any]]:
        parsed = urlsplit(target)
        path = parsed.path
        if method == "GET" and path == "/healthz":
            return 200, {"status": "ok"}
        if method == "POST" and path == "/command":
            if not self.allow_command_route:
                return 404, {"error": "not_found"}
            return self._command(body)
        if method == "POST" and path == "/device/hello":
            return self._device_hello(body, headers or {})
        if method == "POST" and path == "/device/command":
            return self._device_command(body, headers or {})
        if method == "POST" and path == "/device/audio":
            return self._device_audio(body, headers or {}, parsed.query)
        return 404, {"error": "not_found"}

    def _command(self, body: bytes | None) -> tuple[int, dict[str, Any]]:
        payload, error = _parse_json(body)
        if error:
            return 400, {"error": error}
        return self._run_command(payload)

    def _device_hello(self, body: bytes | None, headers: Mapping[str, str]) -> tuple[int, dict[str, Any]]:
        payload, error = _parse_json(body or b"{}")
        if error:
            return 400, {"error": error}

        device_id = str(payload.get("device_id") or "").strip() or f"device-{uuid4().hex[:8]}"
        token_hash = _token_hash(_bearer_token(headers))
        if self.require_device_token and not token_hash:
            return 401, {"error": "device token is required"}
        pairing = self.store.get_device_pairing(device_id)
        if pairing is not None:
            auth_error = _authorize_pairing(pairing["token_hash"], token_hash)
            if auth_error is not None:
                return auth_error

        firmware = str(payload.get("firmware") or "")
        capabilities = payload.get("capabilities") or []
        if not isinstance(capabilities, list):
            return 400, {"error": "capabilities must be an array"}
        self.store.upsert_device_pairing(
            device_id=device_id,
            name=str(payload.get("name") or device_id),
            token_hash=token_hash,
            firmware=firmware,
            capabilities=capabilities,
        )

        session_id = str(payload.get("session_id") or self.store.create_session("device"))
        self.store.append_event(
            session_id,
            "device.hello",
            {
                "device_id": device_id,
                "firmware": firmware,
                "capabilities": capabilities,
                "paired": True,
            },
        )
        return 200, {
            "device_id": device_id,
            "session_id": session_id,
            "protocol": "http-json-v1",
            "state": "ready",
            "paired": True,
        }

    def _device_command(self, body: bytes | None, headers: Mapping[str, str]) -> tuple[int, dict[str, Any]]:
        payload, error = _parse_json(body)
        if error:
            return 400, {"error": error}

        device_id = str(payload.get("device_id") or "").strip()
        if not device_id:
            return 400, {"error": "device_id is required"}
        pairing = self.store.get_device_pairing(device_id)
        if pairing is None:
            return 403, {"error": "device is not paired"}
        auth_error = _authorize_pairing(pairing["token_hash"], _token_hash(_bearer_token(headers)))
        if auth_error is not None:
            return auth_error
        self.store.touch_device_pairing(device_id)

        status, result = self._run_command(payload, source={"type": "device", "device_id": device_id})
        if status != 200:
            return status, result

        state = _device_state(result["status"])
        self.store.append_event(
            result["session_id"],
            "device.result",
            {"device_id": device_id, "state": state},
        )
        return 200, {
            "device_id": device_id,
            "session_id": result["session_id"],
            "state": state,
            "display": _short_display_text(result["text"]),
            "result": result,
        }

    def _device_audio(
        self,
        body: bytes | None,
        headers: Mapping[str, str],
        query: str,
    ) -> tuple[int, dict[str, Any]]:
        if not body:
            return 400, {"error": "audio body is required"}
        if len(body) > MAX_DEVICE_AUDIO_BYTES:
            return 413, {"error": "audio body too large"}

        device_id = _query_value(query, "device_id")
        if not device_id:
            return 400, {"error": "device_id is required"}
        pairing = self.store.get_device_pairing(device_id)
        if pairing is None:
            return 403, {"error": "device is not paired"}
        auth_error = _authorize_pairing(pairing["token_hash"], _token_hash(_bearer_token(headers)))
        if auth_error is not None:
            return auth_error
        self.store.touch_device_pairing(device_id)

        target = _query_value(query, "target", "fake") or "fake"
        session_id = _query_value(query, "session_id") or self.store.create_session(target)
        sample_rate = _positive_int(_query_value(query, "sample_rate"), 16000)
        channels = _positive_int(_query_value(query, "channels"), 1)
        language = _query_value(query, "language", "zh") or "zh"

        try:
            transcript = asr_provider_for().transcribe(
                AsrRequest(
                    audio=body,
                    sample_rate=sample_rate,
                    channels=channels,
                    language=language,
                    audio_format="pcm16",
                )
            )
        except ValueError as exc:
            return 400, {"error": str(exc)}

        self.store.append_event(
            session_id,
            "device.audio",
            {
                "device_id": device_id,
                "bytes": len(body),
                "sample_rate": sample_rate,
                "channels": channels,
                "asr_status": transcript.status,
                "asr_summary": transcript.summary,
            },
        )
        if transcript.status != "done" or not transcript.text.strip():
            self.store.set_session_status(session_id, "error")
            return 200, {
                "device_id": device_id,
                "session_id": session_id,
                "state": "error",
                "transcript": transcript.text,
                "summary": transcript.summary,
            }

        status, result = self._run_command(
            {
                "session_id": session_id,
                "target": target,
                "text": transcript.text,
                "context": {
                    "device_id": device_id,
                    "mode": "voice",
                    "sample_rate": sample_rate,
                    "channels": channels,
                },
            },
            source={"type": "device_audio", "device_id": device_id},
        )
        if status != 200:
            return status, result

        state = _device_state(result["status"])
        self.store.append_event(
            result["session_id"],
            "device.result",
            {"device_id": device_id, "state": state, "input": "audio"},
        )
        return 200, {
            "device_id": device_id,
            "session_id": result["session_id"],
            "state": state,
            "transcript": transcript.text,
            "display": _short_display_text(result["text"]),
            "result": result,
        }

    def device_websocket_hello(
        self,
        body: bytes,
        headers: Mapping[str, str],
        target: str = "fake",
    ) -> tuple[int, dict[str, Any]]:
        payload, error = _parse_json(body)
        if error:
            return 400, {"type": "error", "message": error}
        if payload.get("type") != "hello" or payload.get("transport") != "websocket":
            return 400, {"type": "error", "message": "expected websocket hello"}

        device_id = str(headers.get("device-id") or payload.get("device_id") or "").strip()
        if not device_id:
            return 400, {"type": "error", "message": "Device-Id is required"}
        token_hash = _token_hash(_bearer_token(headers))
        if self.require_device_token and not token_hash:
            return 401, {"type": "error", "message": "device token is required"}
        pairing = self.store.get_device_pairing(device_id)
        if pairing is not None:
            auth_error = _authorize_pairing(pairing["token_hash"], token_hash)
            if auth_error is not None:
                status, error_payload = auth_error
                return status, {"type": "error", "message": error_payload["error"]}

        self.store.upsert_device_pairing(
            device_id=device_id,
            name=device_id,
            token_hash=token_hash,
            firmware=str(payload.get("firmware") or "xiaozhi-websocket"),
            capabilities=["websocket", "audio_in", "audio_out"],
        )
        target = target.strip() or "fake"
        session_id = self.store.get_device_route_session(device_id, target)
        if session_id is None:
            session_id = self.store.create_session(target)
        self.store.set_device_route_session(device_id, target, session_id)
        frame_duration = _audio_param_int(payload, "frame_duration", 60)
        self.store.append_event(
            session_id,
            "device.ws.hello",
            {
                "device_id": device_id,
                "target": target,
                "transport": "websocket",
                "version": payload.get("version"),
                "client_id": str(headers.get("client-id") or ""),
                "protocol_version": str(headers.get("protocol-version") or ""),
                "audio_params": payload.get("audio_params") if isinstance(payload.get("audio_params"), dict) else {},
            },
        )
        return 200, {
            "type": "hello",
            "transport": "websocket",
            "session_id": session_id,
            "audio_params": {
                "format": "opus",
                "sample_rate": 16000,
                "channels": 1,
                "frame_duration": frame_duration,
            },
        }

    def device_websocket_control(
        self,
        body: bytes,
        headers: Mapping[str, str],
        session_id: str,
    ) -> tuple[int, dict[str, Any]]:
        payload, error = _parse_json(body)
        if error:
            return 400, {"type": "error", "message": error}

        message_type = str(payload.get("type") or "")
        device_id = str(headers.get("device-id") or payload.get("device_id") or "").strip()
        event_payload: dict[str, Any] = {
            "device_id": device_id,
            "type": message_type,
        }
        if message_type == "listen":
            event_payload["state"] = str(payload.get("state") or "")
            event_payload["mode"] = str(payload.get("mode") or "")
        elif message_type == "abort":
            event_payload["reason"] = str(payload.get("reason") or "")
        elif message_type == "mcp":
            event_payload["payload_type"] = type(payload.get("payload")).__name__
        self.store.append_event(session_id, "device.ws.control", event_payload)
        return 200, {
            "type": message_type,
            "state": str(payload.get("state") or ""),
            "mode": str(payload.get("mode") or ""),
        }

    def device_websocket_audio(
        self,
        audio: bytes,
        headers: Mapping[str, str],
        session_id: str,
        target: str = "fake",
        audio_frames: tuple[bytes, ...] = (),
        frame_duration_ms: int = 20,
    ) -> tuple[int, list[dict[str, Any]]]:
        device_id = str(headers.get("device-id") or "").strip()
        target = target.strip() or "fake"
        if not device_id:
            return 400, [{"type": "error", "message": "Device-Id is required"}]
        if not audio:
            return 400, [{"session_id": session_id, "type": "error", "message": "audio is required"}]
        if len(audio) > MAX_DEVICE_AUDIO_BYTES:
            return 413, [{"session_id": session_id, "type": "error", "message": "audio too large"}]

        asr_started_at = time.monotonic()
        try:
            transcript = asr_provider_for().transcribe(
                AsrRequest(
                    audio=audio,
                    sample_rate=16000,
                    channels=1,
                    language="zh",
                    hints=("中文语音", "唤醒词可能是你好小智或你好小元"),
                    audio_format="opus_frames",
                    frame_duration_ms=frame_duration_ms,
                    audio_frames=audio_frames,
                )
            )
        except ValueError as exc:
            return 400, [{"session_id": session_id, "type": "error", "message": str(exc)}]
        asr_elapsed_ms = int((time.monotonic() - asr_started_at) * 1000)

        self.store.append_event(
            session_id,
            "device.ws.audio",
            {
                "device_id": device_id,
                "bytes": len(audio),
                "asr_status": transcript.status,
                "asr_summary": transcript.summary,
                "asr_elapsed_ms": asr_elapsed_ms,
            },
        )
        print(
            "asr websocket "
            f"session={session_id} device={device_id} bytes={len(audio)} frames={len(audio_frames)} "
            f"status={transcript.status} elapsed_ms={asr_elapsed_ms} summary={transcript.summary} "
            f"text={_short_log_text(transcript.text)}",
            flush=True,
        )
        messages: list[dict[str, Any]] = [
            {"session_id": session_id, "type": "stt", "text": transcript.text}
        ]
        if transcript.status != "done" or not transcript.text.strip():
            self.store.set_session_status(session_id, "error")
            messages.extend(_tts_messages(session_id, "我没听清。"))
            return 200, messages
        if _is_wake_only_transcript(transcript.text):
            messages.extend(_tts_messages(session_id, "我在。"))
            return 200, messages

        status, result = self._run_command(
            {
                "session_id": session_id,
                "target": target,
                "text": transcript.text,
                "context": {
                    "device_id": device_id,
                    "mode": "voice",
                    "transport": "websocket",
                    "sample_rate": 16000,
                    "channels": 1,
                },
            },
            source={"type": "device_ws_audio", "device_id": device_id},
        )
        if status != 200:
            messages.extend(_tts_messages(session_id, "服务处理失败。"))
            return status, messages

        state = _device_state(result["status"])
        self.store.append_event(
            result["session_id"],
            "device.result",
            {"device_id": device_id, "state": state, "input": "websocket_audio"},
        )
        spoken_text = result["text"] if result["status"] == "done" else "服务处理超时，请再试一次。"
        messages.extend(_tts_messages(session_id, spoken_text))
        return 200, messages

    def _run_command(
        self,
        payload: dict[str, Any],
        source: dict[str, Any] | None = None,
    ) -> tuple[int, dict[str, Any]]:
        text = str(payload.get("text") or "").strip()
        target = str(payload.get("target") or "fake").strip()
        if not text:
            return 400, {"error": "text is required"}
        context = payload.get("context") or {}
        if not isinstance(context, dict):
            return 400, {"error": "context must be an object"}

        try:
            adapter = adapter_for(target)
        except ValueError as exc:
            return 400, {"error": str(exc)}

        session_id = str(payload.get("session_id") or self.store.create_session(target))
        self.store.append_event(
            session_id,
            "command.received",
            {"target": target, "text": text, "source": source or {"type": "http"}},
        )
        started_at = time.monotonic()
        response = adapter.run(
            AgentRequest(
                session_id=session_id,
                target=target,
                user_text=text,
                context=context,
            )
        )
        elapsed_ms = int((time.monotonic() - started_at) * 1000)
        source_type = str((source or {"type": "http"}).get("type") or "http")
        print(
            "agent bridge "
            f"session={session_id} target={target} source={source_type} "
            f"status={response.status} elapsed_ms={elapsed_ms}",
            flush=True,
        )
        self.store.append_event(
            session_id,
            "backend.response",
            {
                "status": response.status,
                "text": response.text,
                "summary": response.summary,
                "artifacts": response.artifacts,
                "elapsed_ms": elapsed_ms,
            },
        )
        self.store.set_session_status(session_id, response.status)
        return 200, {
            "session_id": session_id,
            "target": target,
            "status": response.status,
            "text": response.text,
            "summary": response.summary,
            "artifacts": response.artifacts,
        }


def build_server(
    host: str,
    port: int,
    db_path: str | Path,
    require_device_token: bool = False,
    allow_command_route: bool = True,
) -> ThreadingHTTPServer:
    app = BridgeApplication(
        db_path,
        require_device_token=require_device_token,
        allow_command_route=allow_command_route,
    )

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self) -> None:  # noqa: N802
            if urlsplit(self.path).path == "/device/ws":
                self._handle_device_websocket()
                return
            self._handle()

        def do_POST(self) -> None:  # noqa: N802
            self._handle()

        def log_message(self, format: str, *args: object) -> None:
            return

        def _handle(self) -> None:
            length = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(length) if length else None
            headers = {key.lower(): value for key, value in self.headers.items()}
            status, payload = app.handle(self.command, self.path, body, headers)
            print(f"{self.command} {urlsplit(self.path).path} -> {status}", flush=True)
            encoded = json.dumps(payload, ensure_ascii=False, sort_keys=True).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(encoded)))
            self.end_headers()
            self.wfile.write(encoded)

        def _handle_device_websocket(self) -> None:
            headers = {key.lower(): value for key, value in self.headers.items()}
            key = headers.get("sec-websocket-key", "")
            if headers.get("upgrade", "").lower() != "websocket" or not key:
                self.send_error(400, "websocket upgrade required")
                return

            accept = base64.b64encode(hashlib.sha1((key + WEBSOCKET_GUID).encode()).digest()).decode()
            self.send_response(101, "Switching Protocols")
            self.send_header("Upgrade", "websocket")
            self.send_header("Connection", "Upgrade")
            self.send_header("Sec-WebSocket-Accept", accept)
            self.end_headers()
            self.close_connection = True

            try:
                self.connection.settimeout(WEBSOCKET_TURN_TIMEOUT_SECONDS)
                opcode, body = _read_ws_frame(self.rfile)
                if opcode != 1:
                    _write_ws_json(self.wfile, {"type": "error", "message": "expected text hello"})
                    return
                target = _query_value(urlsplit(self.path).query, "target", "fake") or "fake"
                status, payload = app.device_websocket_hello(body, headers, target)
                _write_ws_json(self.wfile, payload)
                print(f"GET /device/ws -> {101 if status == 200 else status}", flush=True)
                if status != 200:
                    return
                session_id = str(payload["session_id"])
                frame_duration_ms = _positive_int(str(payload["audio_params"]["frame_duration"]), 20)
                audio_chunks: list[bytes] = []
                audio_bytes = 0
                listen_mode = ""
                while True:
                    opcode, body = _read_ws_frame(self.rfile)
                    if opcode == 8:
                        return
                    if opcode == 2:
                        audio_chunks.append(body)
                        audio_bytes += len(body)
                        if audio_bytes > MAX_DEVICE_AUDIO_BYTES:
                            _write_ws_json(
                                self.wfile,
                                {"session_id": session_id, "type": "error", "message": "audio too large"},
                            )
                            return
                        continue
                    if opcode != 1:
                        _write_ws_json(
                            self.wfile,
                            {"session_id": session_id, "type": "error", "message": "unsupported frame"},
                        )
                        continue
                    control_status, control = app.device_websocket_control(body, headers, session_id)
                    if control_status != 200:
                        _write_ws_json(self.wfile, control)
                        continue
                    if control.get("type") == "listen" and control.get("state") == "start":
                        listen_mode = str(control.get("mode") or "")
                        audio_chunks.clear()
                        audio_bytes = 0
                    if control.get("type") == "listen" and control.get("state") == "stop":
                        audio_frames = tuple(audio_chunks)
                        audio = b"".join(audio_frames)
                        audio_chunks.clear()
                        audio_bytes = 0
                        if listen_mode == "tts_debug":
                            messages = [{"session_id": session_id, "type": "stt", "text": "tts_debug"}]
                            messages.extend(_tts_messages(session_id, _debug_tts_text()))
                        elif listen_mode == "ask_debug":
                            debug_text = _debug_ask_text()
                            status, result = app._run_command(
                                {
                                    "session_id": session_id,
                                    "target": target,
                                    "text": debug_text,
                                    "context": {
                                        "device_id": str(headers.get("device-id") or ""),
                                        "mode": "voice",
                                        "transport": "websocket",
                                    },
                                },
                                source={"type": "device_ws_debug"},
                            )
                            messages = [{"session_id": session_id, "type": "stt", "text": debug_text}]
                            if status == 200:
                                spoken_text = result["text"] if result["status"] == "done" else "服务处理超时，请再试一次。"
                                messages.extend(_tts_messages(session_id, spoken_text))
                            else:
                                messages.extend(_tts_messages(session_id, "服务处理失败。"))
                        else:
                            _, messages = app.device_websocket_audio(
                                audio,
                                headers,
                                session_id,
                                target,
                                audio_frames=audio_frames,
                                frame_duration_ms=frame_duration_ms,
                            )
                        sent_audio_frames = 0
                        sent_audio_bytes = 0
                        stream_aborted = False
                        for message in messages:
                            if stream_aborted:
                                break
                            if message.get("type") == "tts_audio":
                                audio = message["audio"]
                                sent_audio_frames += 1
                                sent_audio_bytes += len(audio)
                                _write_ws_binary(self.wfile, audio)
                            else:
                                _write_ws_json(self.wfile, message)
                                if _tts_streaming_enabled() and message.get("type") == "tts" and message.get("state") == "sentence_start":
                                    frames, audio_bytes, stream_aborted = _write_tts_audio_stream(
                                        self.wfile,
                                        session_id,
                                        str(message.get("text") or ""),
                                        lambda: _read_pending_ws_abort(self.connection, self.rfile, app, headers, session_id),
                                    )
                                    sent_audio_frames += frames
                                    sent_audio_bytes += audio_bytes
                        print(
                            "GET /device/ws -> 200 "
                            f"session={session_id} messages={len(messages)} "
                            f"tts_audio_frames={sent_audio_frames} tts_audio_bytes={sent_audio_bytes}",
                            flush=True,
                        )
            except (BrokenPipeError, ConnectionResetError) as exc:
                print(f"GET /device/ws -> 200 interrupted ({exc.__class__.__name__}: {exc})", flush=True)
            except OSError as exc:
                if str(exc) == "missing websocket header":
                    print("GET /device/ws -> 200 closed", flush=True)
                else:
                    print(f"GET /device/ws -> 400 ({exc.__class__.__name__}: {exc})", flush=True)
            except Exception as exc:
                print(f"GET /device/ws -> 500 ({exc.__class__.__name__}: {exc})", flush=True)

    return ThreadingHTTPServer((host, port), Handler)


def _parse_json(body: bytes | None) -> tuple[dict[str, Any], str | None]:
    if not body:
        return {}, "request body is required"
    try:
        payload = json.loads(body.decode())
    except json.JSONDecodeError:
        return {}, "request body must be valid JSON"
    if not isinstance(payload, dict):
        return {}, "request body must be a JSON object"
    return payload, None


def _query_value(query: str, name: str, default: str = "") -> str:
    values = parse_qs(query, keep_blank_values=True).get(name)
    if not values:
        return default
    return values[0].strip()


def _positive_int(raw: str, default: int) -> int:
    try:
        value = int(raw)
    except ValueError:
        return default
    return value if value > 0 else default


def _audio_param_int(payload: dict[str, Any], name: str, default: int) -> int:
    audio_params = payload.get("audio_params")
    if not isinstance(audio_params, dict):
        return default
    value = audio_params.get(name)
    if not isinstance(value, int):
        return default
    return value if value > 0 else default


def _read_ws_frame(stream: Any) -> tuple[int, bytes]:
    header = stream.read(2)
    if len(header) != 2:
        raise OSError("missing websocket header")
    first, second = header
    opcode = first & 0x0F
    length = second & 0x7F
    if length == 126:
        length = int.from_bytes(stream.read(2), "big")
    elif length == 127:
        length = int.from_bytes(stream.read(8), "big")
    mask = stream.read(4) if second & 0x80 else b""
    payload = stream.read(length)
    if len(payload) != length:
        raise OSError("truncated websocket frame")
    if mask:
        payload = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
    return opcode, payload


def _read_pending_ws_abort(
    sock: Any,
    stream: Any,
    app: BridgeApplication,
    headers: Mapping[str, str],
    session_id: str,
) -> bool:
    readable, _, _ = select.select([sock], [], [], 0)
    if not readable:
        return False
    opcode, body = _read_ws_frame(stream)
    if opcode == 8:
        return True
    if opcode != 1:
        return False
    status, control = app.device_websocket_control(body, headers, session_id)
    return status == 200 and control.get("type") == "abort"


def _write_ws_json(stream: Any, payload: dict[str, Any]) -> None:
    _write_ws_frame(stream, 1, json.dumps(payload, ensure_ascii=False, sort_keys=True).encode())


def _write_ws_binary(stream: Any, payload: bytes) -> None:
    _write_ws_frame(stream, 2, payload)


def _tts_messages(session_id: str, text: str) -> list[dict[str, Any]]:
    spoken_text = _short_spoken_text(text)
    segments = _tts_text_segments(spoken_text)
    messages: list[dict[str, Any]] = [{"session_id": session_id, "type": "tts", "state": "start"}]
    if _tts_streaming_enabled():
        for segment in segments:
            messages.append({"session_id": session_id, "type": "tts", "state": "sentence_start", "text": segment})
        messages.append({"session_id": session_id, "type": "tts", "state": "stop"})
        return messages
    for segment in segments:
        messages.append({"session_id": session_id, "type": "tts", "state": "sentence_start", "text": segment})
        try:
            speech = tts_provider_for().synthesize(TtsRequest(text=segment, voice="xiaoyuan"))
        except ValueError as exc:
            speech = None
            messages.append({"session_id": session_id, "type": "tts", "state": "error", "message": str(exc)})
        if speech is not None:
            print(
                "tts synth "
                f"session={session_id} status={speech.status} bytes={len(speech.audio)} "
                f"content_type={speech.content_type} summary={speech.summary}",
                flush=True,
            )
        if speech is not None and speech.status == "done" and speech.audio:
            for audio_frame in _tts_audio_frames(speech.audio, speech.content_type):
                messages.append(
                    {
                        "session_id": session_id,
                        "type": "tts_audio",
                        "audio": audio_frame,
                        "content_type": speech.content_type,
                    }
                )
    messages.append({"session_id": session_id, "type": "tts", "state": "stop"})
    return messages


def _tts_audio_frames(audio: bytes, content_type: str) -> tuple[bytes, ...]:
    if _tts_audio_codec() == "opus" and content_type in {"audio/pcm", "audio/wav"}:
        frames = _opus_frames_from_audio(audio, content_type)
        if frames:
            return frames
    max_bytes = _positive_int(os.environ.get("XOB_WS_TTS_AUDIO_FRAME_BYTES", ""), 8000)
    if content_type == "audio/pcm":
        return _split_pcm_frames(audio, max_bytes) or (audio,)
    if content_type == "audio/wav":
        return _split_wav_pcm_frames(audio, max_bytes) or (audio,)
    return (audio,)


def _tts_audio_codec() -> str:
    return (os.environ.get("XOB_WS_TTS_AUDIO_CODEC") or "pcm").strip().lower() or "pcm"


def _opus_frames_from_audio(audio: bytes, content_type: str) -> tuple[bytes, ...]:
    if not audio:
        return ()
    frame_duration = str(_positive_int(os.environ.get("XOB_WS_TTS_OPUS_FRAME_MS", ""), 60))
    bitrate = os.environ.get("XOB_WS_TTS_OPUS_BITRATE", "16000").strip() or "16000"
    cmd = [
        "ffmpeg",
        "-hide_banner",
        "-loglevel",
        "error",
    ]
    if content_type == "audio/pcm":
        cmd += ["-f", "s16le", "-ar", "16000", "-ac", "1"]
    cmd += [
        "-i",
        "pipe:0",
        "-ar",
        "16000",
        "-ac",
        "1",
        "-c:a",
        "libopus",
        "-application",
        "voip",
        "-frame_duration",
        frame_duration,
        "-vbr",
        "off",
        "-b:a",
        bitrate,
        "-f",
        "ogg",
        "pipe:1",
    ]
    proc = subprocess.run(cmd, input=audio, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if proc.returncode != 0:
        print(f"tts opus encode failed: {proc.stderr.decode(errors='replace')[:240]}", flush=True)
        return ()
    return _opus_packets_from_ogg(proc.stdout)


def _opus_packets_from_ogg(ogg: bytes) -> tuple[bytes, ...]:
    packets: list[bytes] = []
    pending = bytearray()
    offset = 0
    while offset + 27 <= len(ogg):
        if ogg[offset : offset + 4] != b"OggS":
            return tuple(packets)
        segments = ogg[offset + 26]
        header_end = offset + 27 + segments
        if header_end > len(ogg):
            return tuple(packets)
        lacing = ogg[offset + 27 : header_end]
        body_len = sum(lacing)
        body_start = header_end
        body_end = body_start + body_len
        if body_end > len(ogg):
            return tuple(packets)
        body_pos = body_start
        for seg_len in lacing:
            pending.extend(ogg[body_pos : body_pos + seg_len])
            body_pos += seg_len
            if seg_len < 255:
                packet = bytes(pending)
                pending.clear()
                if not packet.startswith((b"OpusHead", b"OpusTags")):
                    packets.append(packet)
        offset = body_end
    return tuple(packets)


def _split_pcm_frames(pcm: bytes, max_bytes: int) -> tuple[bytes, ...]:
    if len(pcm) <= max_bytes:
        return (pcm,)
    payload_bytes = max_bytes - (max_bytes % 2)
    if payload_bytes < 2:
        return ()
    return tuple(pcm[offset : offset + payload_bytes] for offset in range(0, len(pcm), payload_bytes))


def _write_tts_audio_stream(stream: Any, session_id: str, text: str, abort_requested: Any = None) -> tuple[int, int, bool]:
    frames = 0
    sent_bytes = 0
    started_at = time.monotonic()
    try:
        provider = tts_provider_for()
        stream_audio = getattr(provider, "stream_audio", None)
        if stream_audio is None or _tts_audio_codec() == "opus":
            speech = provider.synthesize(TtsRequest(text=text.strip(), voice="xiaoyuan"))
            audio_chunks = _tts_audio_frames(speech.audio, speech.content_type) if speech.status == "done" else ()
            if (
                speech.status == "done"
                and _tts_audio_codec() != "opus"
                and speech.content_type in {"audio/pcm", "audio/wav"}
            ):
                audio_chunks = _stream_pcm_audio_frames(audio_chunks)
            summary = speech.summary
        else:
            audio_chunks = _stream_pcm_audio_frames(
                chunk for chunk in stream_audio(TtsRequest(text=text.strip(), voice="xiaoyuan"))
            )
            summary = f"{provider.provider} streaming tts"
        for audio in audio_chunks:
            if abort_requested is not None and abort_requested():
                _write_ws_json(stream, {"session_id": session_id, "type": "tts", "state": "stop"})
                print(
                    "tts stream "
                    f"session={session_id} frames={frames} bytes={sent_bytes} "
                    f"elapsed_ms={int((time.monotonic() - started_at) * 1000)} aborted=1",
                    flush=True,
                )
                return frames, sent_bytes, True
            frames += 1
            sent_bytes += len(audio)
            _write_ws_binary(stream, audio)
        print(
            "tts stream "
            f"session={session_id} frames={frames} bytes={sent_bytes} "
            f"elapsed_ms={int((time.monotonic() - started_at) * 1000)} summary={summary}",
            flush=True,
        )
    except Exception as exc:
        _write_ws_json(
            stream,
            {
                "session_id": session_id,
                "type": "tts",
                "state": "error",
                "message": _short_display_text(str(exc)),
            },
        )
        print(
            "tts stream "
            f"session={session_id} elapsed_ms={int((time.monotonic() - started_at) * 1000)} "
            f"error={_short_log_text(str(exc))}",
            flush=True,
        )
    return frames, sent_bytes, False


def _tts_streaming_enabled() -> bool:
    return (os.environ.get("XOB_TTS_STREAMING") or "").strip().lower() in {"1", "true", "yes", "on"}


def _debug_tts_text() -> str:
    return (
        os.environ.get("XOB_DEBUG_TTS_TEXT", "").strip()
        or "你好，我是小元。这是一段播放调试语音，用来单独检查音频输出是否连续稳定。现在不走语音识别，也不调用智能体，只测试服务器合成和板端播放。"
    )


def _debug_ask_text() -> str:
    return os.environ.get("XOB_DEBUG_ASK_TEXT", "").strip() or "你好你是谁"


def _stream_pcm_audio_frames(chunks: Any) -> Any:
    frame_bytes = _positive_int(os.environ.get("XOB_WS_TTS_STREAM_FRAME_BYTES", ""), 2000)
    frame_bytes -= frame_bytes % 2
    if frame_bytes < 2:
        frame_bytes = 640

    preroll_ms = _positive_int(os.environ.get("XOB_WS_TTS_STREAM_PREROLL_MS", ""), 120)
    sample_rate = _positive_int(os.environ.get("XOB_BAILIAN_TTS_SAMPLE_RATE", ""), 16000)
    bytes_per_second = max(1, sample_rate) * 2
    preroll_bytes = max(frame_bytes, ((bytes_per_second * preroll_ms) // 1000))
    preroll_bytes -= preroll_bytes % 2
    realtime = (os.environ.get("XOB_WS_TTS_STREAM_REALTIME") or "0").strip().lower() not in {"0", "false", "no", "off"}

    chunk_queue: queue.Queue[bytes | BaseException | None] = queue.Queue(
        maxsize=_positive_int(os.environ.get("XOB_WS_TTS_STREAM_QUEUE_SIZE", ""), 32)
    )

    def produce() -> None:
        try:
            for chunk in chunks:
                if chunk:
                    chunk_queue.put(chunk)
        except BaseException as exc:
            chunk_queue.put(exc)
        finally:
            chunk_queue.put(None)

    threading.Thread(target=produce, name="xob_tts_stream", daemon=True).start()

    pending = bytearray()
    started = False
    stream_started_at = 0.0
    sent_bytes = 0
    while True:
        chunk = chunk_queue.get()
        if chunk is None:
            break
        if isinstance(chunk, BaseException):
            raise chunk
        pending.extend(chunk)
        if not started and len(pending) < preroll_bytes:
            continue
        started = True
        if stream_started_at == 0.0:
            stream_started_at = time.monotonic()
        while len(pending) >= frame_bytes:
            if realtime and sent_bytes > 0:
                delay = stream_started_at + (sent_bytes / bytes_per_second) - time.monotonic()
                if delay > 0:
                    time.sleep(delay)
            sent_bytes += frame_bytes
            yield bytes(pending[:frame_bytes])
            del pending[:frame_bytes]

    if pending:
        if len(pending) % 2 == 1:
            pending.append(0)
        while len(pending) > frame_bytes:
            if realtime and sent_bytes > 0:
                delay = stream_started_at + (sent_bytes / bytes_per_second) - time.monotonic()
                if delay > 0:
                    time.sleep(delay)
            sent_bytes += frame_bytes
            yield bytes(pending[:frame_bytes])
            del pending[:frame_bytes]
        if realtime and sent_bytes > 0:
            delay = stream_started_at + (sent_bytes / bytes_per_second) - time.monotonic()
            if delay > 0:
                time.sleep(delay)
        sent_bytes += len(pending)
        yield bytes(pending)


def _split_wav_pcm_frames(wav: bytes, max_bytes: int) -> tuple[bytes, ...]:
    parsed = _parse_wav_pcm16(wav)
    if parsed is None:
        return ()
    _, _, pcm = parsed
    if len(pcm) <= max_bytes:
        return (pcm,)

    block_align = 2
    payload_bytes = max_bytes
    payload_bytes -= payload_bytes % block_align
    if payload_bytes < block_align:
        return ()

    frames: list[bytes] = []
    for offset in range(0, len(pcm), payload_bytes):
        frames.append(pcm[offset : offset + payload_bytes])
    return tuple(frames)


def _parse_wav_pcm16(wav: bytes) -> tuple[int, int, bytes] | None:
    if len(wav) < 12 or wav[:4] != b"RIFF" or wav[8:12] != b"WAVE":
        return None
    fmt: bytes | None = None
    data: bytes | None = None
    offset = 12
    while offset + 8 <= len(wav):
        chunk_id = wav[offset : offset + 4]
        chunk_size = int.from_bytes(wav[offset + 4 : offset + 8], "little")
        chunk_start = offset + 8
        chunk_end = chunk_start + chunk_size
        if chunk_end > len(wav):
            return None
        if chunk_id == b"fmt ":
            fmt = wav[chunk_start:chunk_end]
        elif chunk_id == b"data":
            data = wav[chunk_start:chunk_end]
        offset = chunk_end + (chunk_size % 2)
    if fmt is None or data is None or len(fmt) < 16:
        return None
    audio_format = int.from_bytes(fmt[0:2], "little")
    channels = int.from_bytes(fmt[2:4], "little")
    sample_rate = int.from_bytes(fmt[4:8], "little")
    bits_per_sample = int.from_bytes(fmt[14:16], "little")
    if audio_format != 1 or channels != 1 or sample_rate != 16000 or bits_per_sample != 16:
        return None
    return sample_rate, channels, data


def _write_ws_frame(stream: Any, opcode: int, payload: bytes) -> None:
    if len(payload) < 126:
        header = bytes([0x80 | opcode, len(payload)])
    elif len(payload) < 65536:
        header = bytes([0x80 | opcode, 126]) + len(payload).to_bytes(2, "big")
    else:
        header = bytes([0x80 | opcode, 127]) + len(payload).to_bytes(8, "big")
    stream.write(header + payload)
    stream.flush()


def _bearer_token(headers: Mapping[str, str]) -> str:
    value = headers.get("authorization", "")
    prefix = "Bearer "
    if not value.startswith(prefix):
        return ""
    return value[len(prefix):].strip()


def _token_hash(token: str) -> str:
    if not token:
        return ""
    return "sha256:" + hashlib.sha256(token.encode()).hexdigest()


def _authorize_pairing(stored_hash: str, request_hash: str) -> tuple[int, dict[str, Any]] | None:
    if stored_hash and request_hash != stored_hash:
        return 401, {"error": "invalid device token"}
    return None


def _device_state(status: str) -> str:
    if status == "error":
        return "error"
    if status == "needs_approval":
        return "needs_approval"
    return "result"


def _short_display_text(text: str) -> str:
    single_line = " ".join(text.split())
    if len(single_line) <= 80:
        return single_line
    return single_line[:77] + "..."


def _short_log_text(text: str) -> str:
    single_line = " ".join(text.split())
    if not single_line:
        return "<empty>"
    return _short_display_text(single_line)


def _short_spoken_text(text: str) -> str:
    single_line = " ".join(text.split())
    max_chars = _positive_int(os.environ.get("XOB_TTS_SPOKEN_MAX_CHARS", ""), 0)
    if max_chars <= 0:
        return single_line
    if len(single_line) <= max_chars:
        return single_line
    return single_line[:max(1, max_chars)].rstrip("，,。.!?！？") + "。"


def _tts_text_segments(text: str) -> tuple[str, ...]:
    single_line = " ".join(text.split()).strip()
    if not single_line:
        return ("我没听清。",)
    max_chars = _positive_int(os.environ.get("XOB_TTS_SEGMENT_MAX_CHARS", ""), 60)
    if max_chars <= 0:
        max_chars = 60
    parts = re.split(r"([。！？!?；;，,\n])", single_line)
    segments: list[str] = []
    current = ""
    for index in range(0, len(parts), 2):
        piece = parts[index]
        punct = parts[index + 1] if index + 1 < len(parts) else ""
        token = (piece + punct).strip()
        if not token:
            continue
        if current and len(current) + len(token) > max_chars:
            segments.extend(_split_long_tts_segment(current, max_chars))
            current = token
        else:
            current += token
    if current:
        segments.extend(_split_long_tts_segment(current, max_chars))
    return tuple(segments) or ("我没听清。",)


def _split_long_tts_segment(text: str, max_chars: int) -> list[str]:
    if len(text) <= max_chars:
        return [text]
    return [text[offset : offset + max_chars] for offset in range(0, len(text), max_chars)]


def _is_wake_only_transcript(text: str) -> bool:
    normalized = "".join(ch for ch in text if ch.isalnum())
    return normalized in {"你好小智", "你好小元", "小智", "小元"}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8788)
    parser.add_argument("--db", default="data/bridge.sqlite3")
    parser.add_argument("--require-device-token", action="store_true")
    parser.add_argument("--disable-command-route", action="store_true")
    args = parser.parse_args()
    server = build_server(
        args.host,
        args.port,
        args.db,
        require_device_token=args.require_device_token,
        allow_command_route=not args.disable_command_route,
    )
    print(f"listening on http://{args.host}:{args.port}")
    server.serve_forever()


if __name__ == "__main__":
    main()
