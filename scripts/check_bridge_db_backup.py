from __future__ import annotations

import sqlite3
import tempfile
from pathlib import Path

from bridge_db_backup import backup_db, restore_db


def main() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        db = root / "bridge.sqlite3"
        with sqlite3.connect(db) as conn:
            conn.execute("CREATE TABLE events(id INTEGER PRIMARY KEY, text TEXT)")
            conn.execute("INSERT INTO events(text) VALUES (?)", ("hello",))

        backup = backup_db(db, root / "backups")
        restored = root / "restored.sqlite3"
        restore_db(backup, restored, replace=False)
        with sqlite3.connect(restored) as conn:
            row = conn.execute("SELECT text FROM events").fetchone()
        assert row == ("hello",)
    print("check_bridge_db_backup ok")


if __name__ == "__main__":
    main()
