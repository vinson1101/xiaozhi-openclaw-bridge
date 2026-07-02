# Phase 1 Text Bridge MVP

Phase 1 proves the server-side command path before firmware or audio work.

## Scope

- HTTP health endpoint
- HTTP text command endpoint
- fake backend adapter
- SQLite session and event store
- command-line smoke test

No board firmware is required for this phase.

## Run

```bash
PYTHONPATH=src python3 -m xiaozhi_openclaw_bridge.server --host 127.0.0.1 --port 8788 --db data/bridge.sqlite3
```

Health:

```bash
curl http://127.0.0.1:8788/healthz
```

Command:

```bash
curl -sS http://127.0.0.1:8788/command \
  -H 'Content-Type: application/json' \
  -d '{"target":"fake","text":"让龙虾检查今天任务状态"}'
```

## Smoke Test

```bash
python3 scripts/smoke_text_bridge.py
```

Expected output:

```text
smoke_text_bridge ok
```

## Storage

The MVP stores ordered events in SQLite:

- `sessions`
- `session_events`

Local database files live under `data/` and are ignored by Git.

## Next

Phase 2 replaces the fake backend with the first safe OpenClaw adapter.
