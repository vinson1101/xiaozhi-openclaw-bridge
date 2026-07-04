from __future__ import annotations

import json
import sqlite3
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from xiaozhi_openclaw_bridge.asr import AsrRequest, asr_provider_for  # noqa: E402
from xiaozhi_openclaw_bridge.server import BridgeApplication  # noqa: E402


def main() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        db = Path(tmp) / "bridge.sqlite3"
        asr = asr_provider_for("fake")
        transcript = asr.transcribe(AsrRequest(audio=b"\0" * 320, sample_rate=16000, channels=1))
        assert transcript.status == "done"

        app = BridgeApplication(db)
        status, payload = app.handle(
            "POST",
            "/command",
            json.dumps({"target": "fake", "text": transcript.text}, ensure_ascii=False).encode(),
        )
        assert status == 200
        assert payload["status"] == "done"
        assert "Fake 后端" in payload["text"]

        with sqlite3.connect(db) as conn:
            row = conn.execute(
                "SELECT payload_json FROM session_events WHERE session_id = ? AND type = ?",
                (payload["session_id"], "command.received"),
            ).fetchone()
        assert row is not None
        assert json.loads(row[0])["text"] == transcript.text
    print("check_voice_route ok")


if __name__ == "__main__":
    main()
