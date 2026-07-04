from __future__ import annotations

import json
import sqlite3
import sys
import tempfile
import threading
import time
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from xiaozhi_openclaw_bridge.server import build_server  # noqa: E402


def main() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        db = Path(tmp) / "bridge.sqlite3"
        server = build_server("127.0.0.1", 0, db)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        logs = StringIO()
        thread.start()
        try:
            host, port = server.server_address
            with redirect_stdout(logs):
                _wait_for_health(host, port)
                base = f"http://{host}:{port}"
                token = "dev-pairing-token"
                hello = _post_json(
                    f"{base}/device/hello",
                    {
                        "device_id": "sim-esp32-c3",
                        "name": "simulator",
                        "firmware": "simulator",
                        "capabilities": ["display", "text"],
                    },
                    token=token,
                )
                assert hello["state"] == "ready"
                assert hello["paired"] is True
                reconnect = _post_json(
                    f"{base}/device/hello",
                    {
                        "device_id": hello["device_id"],
                        "session_id": hello["session_id"],
                        "name": "simulator",
                        "firmware": "simulator",
                        "capabilities": ["display", "text"],
                    },
                    token=token,
                )
                assert reconnect["state"] == "ready"
                assert reconnect["session_id"] == hello["session_id"]
                missing_auth = _post_json_expect_error(
                    f"{base}/device/command",
                    {
                        "device_id": hello["device_id"],
                        "session_id": hello["session_id"],
                        "target": "fake",
                        "text": "这条不应该执行",
                    },
                )
                assert missing_auth["status"] == 401
                assert missing_auth["payload"]["error"] == "invalid device token"
                command = _post_json(
                    f"{base}/device/command",
                    {
                        "device_id": hello["device_id"],
                        "session_id": hello["session_id"],
                        "target": "fake",
                        "text": "你好，检查链路",
                    },
                    token=token,
                )
            assert command["state"] == "result"
            assert "Fake 后端" in command["result"]["text"]
            log_text = logs.getvalue()
            assert "GET /healthz -> 200" in log_text
            assert "POST /device/hello -> 200" in log_text
            assert "POST /device/command -> 401" in log_text
            assert token not in log_text
            assert "你好，检查链路" not in log_text
            with sqlite3.connect(db) as conn:
                pairing = conn.execute(
                    "SELECT device_id, name, token_hash, firmware, capabilities_json FROM device_pairings WHERE device_id = ?",
                    (hello["device_id"],),
                ).fetchone()
                assert pairing is not None
                assert pairing[1] == "simulator"
                assert pairing[2].startswith("sha256:")
                assert token not in pairing[2]
                assert pairing[3] == "simulator"
                rows = conn.execute(
                    "SELECT type FROM session_events WHERE session_id = ? ORDER BY seq",
                    (hello["session_id"],),
                ).fetchall()
            assert [row[0] for row in rows] == [
                "device.hello",
                "device.hello",
                "command.received",
                "backend.response",
                "device.result",
            ]
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)
        secure_db = Path(tmp) / "secure.sqlite3"
        secure_server = build_server("127.0.0.1", 0, secure_db, require_device_token=True)
        secure_thread = threading.Thread(target=secure_server.serve_forever, daemon=True)
        secure_thread.start()
        try:
            host, port = secure_server.server_address
            _wait_for_health(host, port)
            denied = _post_json_expect_error(
                f"http://{host}:{port}/device/hello",
                {"device_id": "needs-token"},
            )
            assert denied["status"] == 401
            assert denied["payload"]["error"] == "device token is required"
        finally:
            secure_server.shutdown()
            secure_server.server_close()
            secure_thread.join(timeout=2)
    print("smoke_device_http ok")


def _wait_for_health(host: str, port: int) -> None:
    for _ in range(50):
        try:
            with urlopen(f"http://{host}:{port}/healthz", timeout=0.2) as res:
                if res.status == 200:
                    return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("server did not become healthy")


def _post_json(url: str, payload: dict[str, object], token: str = "") -> dict[str, object]:
    return _post_json_with_status(url, payload, token=token)[1]


def _post_json_expect_error(url: str, payload: dict[str, object]) -> dict[str, object]:
    try:
        _post_json_with_status(url, payload, token="")
    except HTTPError as exc:
        return {"status": exc.code, "payload": json.loads(exc.read().decode())}
    raise AssertionError("request unexpectedly succeeded")


def _post_json_with_status(url: str, payload: dict[str, object], token: str = "") -> tuple[int, dict[str, object]]:
    headers = {"Content-Type": "application/json"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    req = Request(
        url,
        data=json.dumps(payload, ensure_ascii=False).encode(),
        headers=headers,
        method="POST",
    )
    with urlopen(req, timeout=2) as res:
        return res.status, json.loads(res.read().decode())


if __name__ == "__main__":
    main()
