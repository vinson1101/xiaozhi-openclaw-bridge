from __future__ import annotations

import argparse
import hashlib
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Mapping
from urllib.parse import urlsplit
from uuid import uuid4

from xiaozhi_openclaw_bridge.adapters import AgentRequest, adapter_for
from xiaozhi_openclaw_bridge.store import EventStore


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
        path = urlsplit(target).path
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
