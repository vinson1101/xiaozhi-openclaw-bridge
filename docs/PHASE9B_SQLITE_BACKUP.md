# Phase 9B SQLite Backup

Phase 9B adds a small SQLite backup and restore helper for the Bridge event
store.

## Result

The repository includes:

```text
scripts/bridge_db_backup.py
scripts/check_bridge_db_backup.py
```

Create a backup:

```sh
python3 scripts/bridge_db_backup.py backup /var/lib/xob-bridge/bridge.sqlite3 /var/backups/xob-bridge
```

Restore a backup:

```sh
python3 scripts/bridge_db_backup.py restore /var/backups/xob-bridge/bridge-YYYYMMDDTHHMMSSZ.sqlite3 /var/lib/xob-bridge/bridge.sqlite3 --replace
```

The helper uses SQLite's online backup API and runs `PRAGMA integrity_check`
before accepting a backup or restore source.

## Boundaries

- No remote object storage.
- No encryption wrapper yet.
- No scheduled timer yet.
- The restore command refuses to overwrite an existing database unless
  `--replace` is passed.

## Validation

```text
python3 scripts/check_bridge_db_backup.py
```
