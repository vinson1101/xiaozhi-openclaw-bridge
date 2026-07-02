# Phase 2 OpenClaw Adapter

Phase 2 adds the first real backend adapter for OpenClaw.

## Scope

- `target:"openclaw"` in `POST /command`
- SSH-based OpenClaw CLI invocation
- safe default mode that only checks `openclaw health --json`
- optional command mode using `openclaw agent --json --message ...`
- fixture smoke test with a fake `ssh` executable

No VPS host, SSH key, token, or personal runtime path is stored in Git.

## Configuration

Set these in a local `.env` or shell profile. `.env` is ignored by Git.

```bash
export XOB_OPENCLAW_SSH_TARGET='<user>@<host>'
export XOB_OPENCLAW_SSH_KEY='/absolute/path/to/private/key'
export XOB_OPENCLAW_SSH_KNOWN_HOSTS='/absolute/path/to/known_hosts'
```

By default, the adapter does not forward user commands. It only verifies that the OpenClaw gateway is reachable:

```bash
curl -sS http://127.0.0.1:8788/command \
  -H 'Content-Type: application/json' \
  -d '{"target":"openclaw","text":"检查状态"}'
```

Enable real command forwarding explicitly:

```bash
export XOB_OPENCLAW_ENABLE_COMMANDS=1
export XOB_OPENCLAW_AGENT='<optional-agent-id>'
export XOB_OPENCLAW_SESSION_PREFIX='xiaozhi-bridge'
```

Then the bridge calls:

```text
ssh <target> openclaw agent --json --message <text> --session-key <prefix>:<session_id>
```

The implementation quotes remote shell arguments before sending them through SSH.

## Run

```bash
PYTHONPATH=src python3 -m xiaozhi_openclaw_bridge.server --host 127.0.0.1 --port 8788 --db data/bridge.sqlite3
```

Command:

```bash
curl -sS http://127.0.0.1:8788/command \
  -H 'Content-Type: application/json' \
  -d '{"target":"openclaw","text":"让龙虾总结今天任务状态"}'
```

## Smoke Test

```bash
python3 scripts/smoke_openclaw_ssh_adapter.py
```

Expected output:

```text
smoke_openclaw_ssh_adapter ok
```

## Safety Notes

- Keep OpenClaw gateway loopback-only on the VPS.
- Prefer SSH or a private tunnel from the bridge host.
- Keep command forwarding disabled until the selected OpenClaw agent policy is clear.
- Preserve approval responses as backend responses; the bridge should not bypass OpenClaw approvals.
