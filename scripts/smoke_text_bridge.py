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
            payload = {"target": "fake", "text": "让龙虾检查今天任务状态"}
            response = _post_json(f"http://{host}:{port}/command", payload)
            assert response["status"] == "done"
            assert "Fake 后端" in response["text"]
            session_id = response["session_id"]
            with sqlite3.connect(db) as conn:
                events = conn.execute(
                    "SELECT type FROM session_events WHERE session_id = ? ORDER BY seq",
                    (session_id,),
                ).fetchall()
            assert [row[0] for row in events] == ["command.received", "backend.response"]
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)
    print("smoke_text_bridge ok")


def _wait_for_health(host: str, port: int) -> None:
    for _ in range(50):
        try:
            with urlopen(f"http://{host}:{port}/healthz", timeout=0.2) as res:
                if res.status == 200:
                    return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("server did not become healthy")


def _post_json(url: str, payload: dict[str, str]) -> dict[str, str]:
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
