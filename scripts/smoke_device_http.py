from __future__ import annotations

import json
import sqlite3
import sys
import tempfile
import threading
import time
from pathlib import Path
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from xiaozhi_openclaw_bridge.server import build_server  # noqa: E402


def main() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        db = Path(tmp) / "bridge.sqlite3"
        server = build_server("127.0.0.1", 0, db)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            host, port = server.server_address
            _wait_for_health(host, port)
            base = f"http://{host}:{port}"
            hello = _post_json(
                f"{base}/device/hello",
                {
                    "device_id": "sim-esp32-c3",
                    "firmware": "simulator",
                    "capabilities": ["display", "text"],
                },
            )
            assert hello["state"] == "ready"
            command = _post_json(
                f"{base}/device/command",
                {
                    "device_id": hello["device_id"],
                    "session_id": hello["session_id"],
                    "target": "fake",
                    "text": "你好，检查链路",
                },
            )
            assert command["state"] == "result"
            assert "Fake 后端" in command["result"]["text"]
            with sqlite3.connect(db) as conn:
                rows = conn.execute(
                    "SELECT type FROM session_events WHERE session_id = ? ORDER BY seq",
                    (hello["session_id"],),
                ).fetchall()
            assert [row[0] for row in rows] == [
                "device.hello",
                "command.received",
                "backend.response",
                "device.result",
            ]
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)
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


def _post_json(url: str, payload: dict[str, object]) -> dict[str, object]:
    req = Request(
        url,
        data=json.dumps(payload, ensure_ascii=False).encode(),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urlopen(req, timeout=2) as res:
        return json.loads(res.read().decode())


if __name__ == "__main__":
    main()
