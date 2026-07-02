from __future__ import annotations

import json
import sqlite3
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from uuid import uuid4


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


class EventStore:
    def __init__(self, path: str | Path) -> None:
        self.path = Path(path)

    def connect(self) -> sqlite3.Connection:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        conn = sqlite3.connect(self.path)
        conn.row_factory = sqlite3.Row
        return conn

    def init(self) -> None:
        with self.connect() as conn:
            conn.executescript(
                """
                CREATE TABLE IF NOT EXISTS sessions (
                    id TEXT PRIMARY KEY,
                    target TEXT NOT NULL,
                    status TEXT NOT NULL,
                    created_at TEXT NOT NULL,
                    updated_at TEXT NOT NULL
                );

                CREATE TABLE IF NOT EXISTS session_events (
                    id TEXT PRIMARY KEY,
                    session_id TEXT NOT NULL,
                    seq INTEGER NOT NULL,
                    type TEXT NOT NULL,
                    payload_json TEXT NOT NULL,
                    created_at TEXT NOT NULL,
                    UNIQUE(session_id, seq),
                    FOREIGN KEY(session_id) REFERENCES sessions(id)
                );
                """
            )

    def create_session(self, target: str) -> str:
        session_id = str(uuid4())
        now = utc_now()
        with self.connect() as conn:
            conn.execute(
                """
                INSERT INTO sessions (id, target, status, created_at, updated_at)
                VALUES (?, ?, ?, ?, ?)
                """,
                (session_id, target, "created", now, now),
            )
        return session_id

    def set_session_status(self, session_id: str, status: str) -> None:
        with self.connect() as conn:
            conn.execute(
                "UPDATE sessions SET status = ?, updated_at = ? WHERE id = ?",
                (status, utc_now(), session_id),
            )

    def append_event(self, session_id: str, event_type: str, payload: dict[str, Any]) -> int:
        with self.connect() as conn:
            row = conn.execute(
                "SELECT COALESCE(MAX(seq), 0) + 1 AS next_seq FROM session_events WHERE session_id = ?",
                (session_id,),
            ).fetchone()
            seq = int(row["next_seq"])
            conn.execute(
                """
                INSERT INTO session_events (id, session_id, seq, type, payload_json, created_at)
                VALUES (?, ?, ?, ?, ?, ?)
                """,
                (
                    str(uuid4()),
                    session_id,
                    seq,
                    event_type,
                    json.dumps(payload, ensure_ascii=False, sort_keys=True),
                    utc_now(),
                ),
            )
        return seq

    def list_events(self, session_id: str) -> list[sqlite3.Row]:
        with self.connect() as conn:
            return list(
                conn.execute(
                    """
                    SELECT session_id, seq, type, payload_json, created_at
                    FROM session_events
                    WHERE session_id = ?
                    ORDER BY seq
                    """,
                    (session_id,),
                )
            )
