from __future__ import annotations

import argparse
import sqlite3
from datetime import datetime, timezone
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    subcommands = parser.add_subparsers(dest="command", required=True)

    backup = subcommands.add_parser("backup")
    backup.add_argument("db")
    backup.add_argument("backup_dir")

    restore = subcommands.add_parser("restore")
    restore.add_argument("backup")
    restore.add_argument("db")
    restore.add_argument("--replace", action="store_true")

    args = parser.parse_args()
    if args.command == "backup":
        out = backup_db(Path(args.db), Path(args.backup_dir))
        print(out)
    elif args.command == "restore":
        restore_db(Path(args.backup), Path(args.db), replace=args.replace)
        print("restore ok")


def backup_db(db_path: Path, backup_dir: Path) -> Path:
    if not db_path.exists():
        raise SystemExit(f"database not found: {db_path}")
    backup_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    out = backup_dir / f"{db_path.stem}-{stamp}.sqlite3"

    with sqlite3.connect(f"file:{db_path}?mode=ro", uri=True) as src, sqlite3.connect(out) as dst:
        src.backup(dst)
    _check_integrity(out)
    return out


def restore_db(backup_path: Path, db_path: Path, *, replace: bool) -> None:
    if not backup_path.exists():
        raise SystemExit(f"backup not found: {backup_path}")
    _check_integrity(backup_path)
    if db_path.exists() and not replace:
        raise SystemExit(f"database exists, pass --replace to overwrite: {db_path}")
    db_path.parent.mkdir(parents=True, exist_ok=True)
    tmp = db_path.with_suffix(db_path.suffix + ".tmp")
    if tmp.exists():
        tmp.unlink()
    with sqlite3.connect(f"file:{backup_path}?mode=ro", uri=True) as src, sqlite3.connect(tmp) as dst:
        src.backup(dst)
    tmp.replace(db_path)


def _check_integrity(db_path: Path) -> None:
    with sqlite3.connect(f"file:{db_path}?mode=ro", uri=True) as conn:
        result = conn.execute("PRAGMA integrity_check").fetchone()
    if result != ("ok",):
        raise SystemExit(f"sqlite integrity check failed for {db_path}: {result}")


if __name__ == "__main__":
    main()
