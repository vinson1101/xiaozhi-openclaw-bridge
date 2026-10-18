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
import os
import shlex
import sys
from pathlib import Path

remote_command = sys.argv[-1]
argv = shlex.split(remote_command)
ssh_target = sys.argv[-2] if len(sys.argv) > 2 else ""
if " health " in f" {remote_command} ":
    print(json.dumps({"ok": True, "status": "live"}))
elif " agent " in f" {remote_command} ":
    text = "语音短答" if "语音输出要求" in remote_command else "龙虾收到：测试命令"
    print(json.dumps({"status": "ok", "result": {"payloads": [{"text": text}]}}))
elif " -z " in f" {remote_command} ":
    if ssh_target == "fake-hermes" and " --safe-mode " not in f" {remote_command} ":
        print("missing --safe-mode", file=sys.stderr)
        sys.exit(2)
    if ssh_target == "fake-hermes" and " --toolsets safe " not in f" {remote_command} ":
        print("missing --toolsets safe", file=sys.stderr)
        sys.exit(2)
    print("Hermes收到：测试命令")
elif " chat " in f" {remote_command} ":
    session_dir = Path(os.environ["FAKE_HERMES_SESSION_DIR"])
    if "--continue" in argv:
        title = argv[argv.index("--continue") + 1]
        if not (session_dir / title.replace("/", "_")).exists():
            print(f"No session found matching '{title}'.", file=sys.stderr)
            sys.exit(1)
    if ssh_target == "fake-hermes" and "--skills" in argv:
        if "--toolsets" not in argv or argv[argv.index("--toolsets") + 1] != "browser":
            print("missing --toolsets browser", file=sys.stderr)
            sys.exit(2)
        if argv[argv.index("--skills") + 1] != "xiaoyuan-browser-demo":
            print("missing --skills xiaoyuan-browser-demo", file=sys.stderr)
            sys.exit(2)
    elif ssh_target == "fake-hermes":
        if "--safe-mode" not in argv:
            print("missing --safe-mode", file=sys.stderr)
            sys.exit(2)
        if "--toolsets" not in argv or argv[argv.index("--toolsets") + 1] != "safe":
            print("missing --toolsets safe", file=sys.stderr)
            sys.exit(2)
    print("reasoning output ignored")
    print("session_id: fake-hermes-session")
    print("Hermes技能收到：测试命令" if "--skills" in argv else "Hermes收到：测试命令")
elif " sessions rename " in f" {remote_command} ":
    session_dir = Path(os.environ["FAKE_HERMES_SESSION_DIR"])
    session_dir.mkdir(parents=True, exist_ok=True)
    title = argv[-1]
    (session_dir / title.replace("/", "_")).write_text(argv[-2], encoding="utf-8")
    print("renamed")
else:
    print("unexpected command", remote_command, file=sys.stderr)
    sys.exit(2)
""",
            encoding="utf-8",
        )
        fake_ssh.chmod(0o700)
        fake_openclaw = tmp_path / "openclaw"
        fake_openclaw.write_text(
            """#!/usr/bin/env python3
import json
import sys

if "health" in sys.argv:
    print(json.dumps({"ok": True, "status": "live"}))
elif "agent" in sys.argv:
    print(json.dumps({"status": "ok", "result": {"payloads": [{"text": "龙虾收到：测试命令"}]}}))
else:
    print("unexpected argv", sys.argv, file=sys.stderr)
    sys.exit(2)
""",
            encoding="utf-8",
        )
        fake_openclaw.chmod(0o700)

        old_env = dict(os.environ)
        try:
            os.environ.update(
                {
                    "XOB_OPENCLAW_SSH_BIN": str(fake_ssh),
                    "XOB_OPENCLAW_SSH_TARGET": "fake-openclaw",
                    "XOB_OPENCLAW_ENABLE_COMMANDS": "1",
                }
            )
            _run_command_smoke(
                tmp_path / "bridge-ssh.sqlite3",
                "openclaw",
                context={"mode": "voice"},
                expected_text="语音短答",
            )

            os.environ.clear()
            os.environ.update(old_env)
            os.environ.update(
                {
                    "PATH": f"{tmp_path}:{old_env.get('PATH', '')}",
                    "XOB_AGENT_TARGETS": "hermes=hermes-cli:XOB_HERMES",
                    "XOB_HERMES_SSH_BIN": str(fake_ssh),
                    "XOB_HERMES_SSH_TARGET": "fake-hermes",
                    "XOB_HERMES_CLI_BIN": "hermes",
                    "XOB_HERMES_ENABLE_COMMANDS": "1",
                    "XOB_HERMES_SAFE_MODE": "1",
                    "XOB_HERMES_TOOLSETS": "safe",
                    "FAKE_HERMES_SESSION_DIR": str(tmp_path / "hermes-sessions"),
                }
            )
            _run_command_smoke(
                tmp_path / "bridge-hermes.sqlite3",
                "hermes",
                expected_text="Hermes收到：测试命令",
                repeat=True,
            )

            os.environ.pop("XOB_HERMES_SAFE_MODE")
            os.environ["XOB_HERMES_TOOLSETS"] = "browser"
            os.environ["XOB_HERMES_SKILLS"] = "xiaoyuan-browser-demo"
            _run_command_smoke(
                tmp_path / "bridge-hermes-skill.sqlite3",
                "hermes",
                expected_text="Hermes技能收到：测试命令",
            )
        finally:
            os.environ.clear()
            os.environ.update(old_env)
    print("smoke_openclaw_ssh_adapter ok")


def _run_command_smoke(
    db: Path,
    target: str,
    context: dict[str, str] | None = None,
    expected_text: str = "龙虾收到：测试命令",
    repeat: bool = False,
) -> None:
    server = build_server("127.0.0.1", 0, db)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        host, port = server.server_address
        _wait_for_health(host, port)
        payload: dict[str, object] = {"target": target, "text": "测试命令"}
        if context is not None:
            payload["context"] = context
        response = _post_json(f"http://{host}:{port}/command", payload)
        assert response["target"] == target
        assert response["status"] == "done"
        assert response["text"] == expected_text
        session_id = response["session_id"]
        if repeat:
            payload["session_id"] = session_id
            response = _post_json(f"http://{host}:{port}/command", payload)
            assert response["session_id"] == session_id
            assert response["target"] == target
            assert response["status"] == "done"
            assert response["text"] == expected_text
        with sqlite3.connect(db) as conn:
            events = conn.execute(
                "SELECT type FROM session_events WHERE session_id = ? ORDER BY seq",
                (session_id,),
            ).fetchall()
        expected_events = ["command.received", "backend.response"]
        if repeat:
            expected_events += ["command.received", "backend.response"]
        assert [row[0] for row in events] == expected_events
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)


def _wait_for_health(host: str, port: int) -> None:
    for _ in range(50):
        try:
            with urlopen(f"http://{host}:{port}/healthz", timeout=0.2) as res:
                if res.status == 200:
                    return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("server did not become healthy")


def _post_json(url: str, payload: dict[str, object]) -> dict[str, str]:
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
