from __future__ import annotations

import json
import os
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
        tmp_path = Path(tmp)
        fake_ssh = tmp_path / "ssh"
        fake_ssh.write_text(
            """#!/usr/bin/env python3
import json
import sys

remote_command = sys.argv[-1]
if " health " in f" {remote_command} ":
    print(json.dumps({"ok": True, "status": "live"}))
elif " agent " in f" {remote_command} ":
    print(json.dumps({"status": "done", "text": "龙虾收到：测试命令", "summary": "ok"}))
else:
    print("unexpected command", remote_command, file=sys.stderr)
    sys.exit(2)
""",
            encoding="utf-8",
        )
        fake_ssh.chmod(0o700)
        db = tmp_path / "bridge.sqlite3"

        old_env = dict(os.environ)
        os.environ.update(
            {
                "XOB_OPENCLAW_SSH_BIN": str(fake_ssh),
                "XOB_OPENCLAW_SSH_TARGET": "fake-openclaw",
                "XOB_OPENCLAW_ENABLE_COMMANDS": "1",
            }
        )
        try:
            server = build_server("127.0.0.1", 0, db)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                host, port = server.server_address
                _wait_for_health(host, port)
                response = _post_json(
                    f"http://{host}:{port}/command",
                    {"target": "openclaw", "text": "测试命令"},
                )
                assert response["status"] == "done"
                assert response["text"] == "龙虾收到：测试命令"
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
        finally:
            os.environ.clear()
            os.environ.update(old_env)
    print("smoke_openclaw_ssh_adapter ok")


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
