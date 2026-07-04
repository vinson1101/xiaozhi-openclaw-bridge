from __future__ import annotations

import sqlite3
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))
sys.path.insert(0, str(ROOT / "scripts"))

from rotate_device_credential import rotate_device_token  # noqa: E402
from xiaozhi_openclaw_bridge.store import EventStore  # noqa: E402


def main() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        db = Path(tmp) / "bridge.sqlite3"
        store = EventStore(db)
        store.init()
        store.upsert_device_pairing(
            device_id="sim-device",
            name="sim-device",
            token_hash="sha256:old",
            firmware="test",
            capabilities=["display"],
        )

        rotate_device_token(db, "sim-device", "rotated-value")
        with sqlite3.connect(db) as conn:
            token_hash = conn.execute(
                "SELECT token_hash FROM device_pairings WHERE device_id = ?",
                ("sim-device",),
            ).fetchone()[0]
        assert token_hash.startswith("sha256:")
        assert "rotated-value" not in token_hash
        assert token_hash != "sha256:old"
    print("check_rotate_device_credential ok")


if __name__ == "__main__":
    main()
