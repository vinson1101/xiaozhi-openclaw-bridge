from __future__ import annotations

import argparse
import base64
import hashlib
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Mapping
from urllib.parse import parse_qs, urlsplit
from uuid import uuid4

from xiaozhi_openclaw_bridge.adapters import AgentRequest, adapter_for
from xiaozhi_openclaw_bridge.asr import AsrRequest, asr_provider_for
from xiaozhi_openclaw_bridge.store import EventStore

MAX_DEVICE_AUDIO_BYTES = 2 * 1024 * 1024
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
                AsrRequest(audio=body, sample_rate=sample_rate, channels=channels, language=language)
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
        session_id = self.store.create_session("device_ws")
        frame_duration = _audio_param_int(payload, "frame_duration", 60)
        self.store.append_event(
            session_id,
            "device.ws.hello",
            {
                "device_id": device_id,
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
                "sample_rate": 24000,
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
    ) -> tuple[int, list[dict[str, Any]]]:
        device_id = str(headers.get("device-id") or "").strip()
        target = target.strip() or "fake"
        if not device_id:
            return 400, [{"type": "error", "message": "Device-Id is required"}]
        if not audio:
            return 400, [{"session_id": session_id, "type": "error", "message": "audio is required"}]
        if len(audio) > MAX_DEVICE_AUDIO_BYTES:
            return 413, [{"session_id": session_id, "type": "error", "message": "audio too large"}]

        try:
            transcript = asr_provider_for().transcribe(
                AsrRequest(audio=audio, sample_rate=16000, channels=1, language="zh")
            )
        except ValueError as exc:
            return 400, [{"session_id": session_id, "type": "error", "message": str(exc)}]

        self.store.append_event(
            session_id,
            "device.ws.audio",
            {
                "device_id": device_id,
                "bytes": len(audio),
                "asr_status": transcript.status,
                "asr_summary": transcript.summary,
            },
        )
        messages: list[dict[str, Any]] = [
            {"session_id": session_id, "type": "stt", "text": transcript.text}
        ]
        if transcript.status != "done" or not transcript.text.strip():
            self.store.set_session_status(session_id, "error")
            messages.append({"session_id": session_id, "type": "tts", "state": "stop"})
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
            messages.append({"session_id": session_id, "type": "tts", "state": "stop"})
            return status, messages

        state = _device_state(result["status"])
        self.store.append_event(
            result["session_id"],
            "device.result",
            {"device_id": device_id, "state": state, "input": "websocket_audio"},
        )
        messages.extend(
            [
                {"session_id": session_id, "type": "tts", "state": "start"},
                {
                    "session_id": session_id,
                    "type": "tts",
                    "state": "sentence_start",
                    "text": _short_display_text(result["text"]),
                },
                {"session_id": session_id, "type": "tts", "state": "stop"},
            ]
        )
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
        response = adapter.run(
            AgentRequest(
                session_id=session_id,
                target=target,
                user_text=text,
                context=context,
            )
        )
        self.store.append_event(
            session_id,
            "backend.response",
            {
                "status": response.status,
                "text": response.text,
                "summary": response.summary,
                "artifacts": response.artifacts,
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
                self.connection.settimeout(10)
                opcode, body = _read_ws_frame(self.rfile)
                if opcode != 1:
                    _write_ws_json(self.wfile, {"type": "error", "message": "expected text hello"})
                    return
                status, payload = app.device_websocket_hello(body, headers)
                _write_ws_json(self.wfile, payload)
                print(f"GET /device/ws -> {101 if status == 200 else status}", flush=True)
                if status != 200:
                    return
                session_id = str(payload["session_id"])
                target = _query_value(urlsplit(self.path).query, "target", "fake") or "fake"
                audio_chunks: list[bytes] = []
                audio_bytes = 0
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
                    if control.get("type") == "listen" and control.get("state") == "stop":
                        audio = b"".join(audio_chunks)
                        audio_chunks.clear()
                        audio_bytes = 0
                        _, messages = app.device_websocket_audio(audio, headers, session_id, target)
                        for message in messages:
                            _write_ws_json(self.wfile, message)
            except OSError:
                print("GET /device/ws -> 400", flush=True)

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


def _write_ws_json(stream: Any, payload: dict[str, Any]) -> None:
    _write_ws_frame(stream, json.dumps(payload, ensure_ascii=False, sort_keys=True).encode())


def _write_ws_frame(stream: Any, payload: bytes) -> None:
    if len(payload) < 126:
        header = bytes([0x81, len(payload)])
    elif len(payload) < 65536:
        header = bytes([0x81, 126]) + len(payload).to_bytes(2, "big")
    else:
        header = bytes([0x81, 127]) + len(payload).to_bytes(8, "big")
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
