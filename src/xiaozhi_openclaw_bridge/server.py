from __future__ import annotations

import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlsplit

from xiaozhi_openclaw_bridge.adapters import AgentRequest, adapter_for
from xiaozhi_openclaw_bridge.store import EventStore


class BridgeApplication:
    def __init__(self, db_path: str | Path) -> None:
        self.store = EventStore(db_path)
        self.store.init()

    def handle(self, method: str, target: str, body: bytes | None) -> tuple[int, dict[str, Any]]:
        path = urlsplit(target).path
        if method == "GET" and path == "/healthz":
            return 200, {"status": "ok"}
        if method == "POST" and path == "/command":
            return self._command(body)
        return 404, {"error": "not_found"}

    def _command(self, body: bytes | None) -> tuple[int, dict[str, Any]]:
        payload, error = _parse_json(body)
        if error:
            return 400, {"error": error}

        text = str(payload.get("text") or "").strip()
        target = str(payload.get("target") or "fake").strip()
        if not text:
            return 400, {"error": "text is required"}

        try:
            adapter = adapter_for(target)
        except ValueError as exc:
            return 400, {"error": str(exc)}

        session_id = str(payload.get("session_id") or self.store.create_session(target))
        self.store.append_event(
            session_id,
            "command.received",
            {"target": target, "text": text},
        )
        response = adapter.run(
            AgentRequest(
                session_id=session_id,
                target=target,
                user_text=text,
                context=dict(payload.get("context") or {}),
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


def build_server(host: str, port: int, db_path: str | Path) -> ThreadingHTTPServer:
    app = BridgeApplication(db_path)

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
            status, payload = app.handle(self.command, self.path, body)
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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8788)
    parser.add_argument("--db", default="data/bridge.sqlite3")
    args = parser.parse_args()
    server = build_server(args.host, args.port, args.db)
    print(f"listening on http://{args.host}:{args.port}")
    server.serve_forever()


if __name__ == "__main__":
    main()
