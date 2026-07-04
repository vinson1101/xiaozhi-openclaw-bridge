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
            "/device/hello",
            json.dumps({"device_id": "voice-device", "capabilities": ["audio_in"]}).encode(),
            {"authorization": "Bearer voice-token"},
        )
        assert status == 200

        status, payload = app.handle(
            "POST",
            "/device/audio?device_id=voice-device&target=fake&sample_rate=16000&channels=1",
            b"\0" * 320,
            {"authorization": "Bearer voice-token", "content-type": "audio/pcm"},
        )
        assert status == 200
        assert payload["transcript"] == transcript.text
        assert payload["state"] == "result"
        assert payload["result"]["status"] == "done"
        assert "Fake 后端" in payload["result"]["text"]

        with sqlite3.connect(db) as conn:
            events = conn.execute(
                "SELECT type, payload_json FROM session_events WHERE session_id = ? ORDER BY seq",
                (payload["session_id"],),
            ).fetchall()
        assert [row[0] for row in events] == [
            "device.audio",
            "command.received",
            "backend.response",
            "device.result",
        ]
        assert json.loads(events[0][1])["bytes"] == 320
        assert "audio" not in events[0][1].lower()
        assert json.loads(events[1][1])["text"] == transcript.text
    print("check_voice_route ok")


if __name__ == "__main__":
    main()
