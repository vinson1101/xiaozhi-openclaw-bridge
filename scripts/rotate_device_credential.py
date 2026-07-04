from __future__ import annotations

import argparse
import getpass
import hashlib
import sqlite3
from datetime import datetime, timezone
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("db")
    parser.add_argument("device_id")
    args = parser.parse_args()

    token = getpass.getpass("new device token: ")
    if not token:
        raise SystemExit("token must not be empty")
    rotate_device_token(Path(args.db), args.device_id, token)
    print("rotate_device_credential ok")


def rotate_device_token(db_path: Path, device_id: str, token: str) -> None:
    if not db_path.exists():
        raise SystemExit(f"database not found: {db_path}")
    if not device_id:
        raise SystemExit("device_id must not be empty")
    if not token:
        raise SystemExit("token must not be empty")

    token_hash = "sha256:" + hashlib.sha256(token.encode()).hexdigest()
    now = datetime.now(timezone.utc).isoformat(timespec="seconds")
    with sqlite3.connect(db_path) as conn:
        row = conn.execute(
            "UPDATE device_pairings SET token_hash = ?, last_seen_at = ? WHERE device_id = ?",
            (token_hash, now, device_id),
        )
        if row.rowcount != 1:
            raise SystemExit(f"device not found: {device_id}")


if __name__ == "__main__":
    main()
