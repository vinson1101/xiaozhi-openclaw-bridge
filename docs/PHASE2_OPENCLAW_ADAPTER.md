# Phase 2 OpenClaw Adapter

Phase 2 adds the first real backend adapter. OpenClaw is the first configured
target, but the Bridge route uses a target table so another environment can
route `hermes` or `lobster` without changing firmware.

## Scope

- `target:"openclaw"` in `POST /command`
- configurable target aliases such as `target:"hermes"`
- SSH-based OpenClaw CLI invocation
- safe default mode that only checks `openclaw health --json`
- optional command mode using `openclaw agent --json --message ...`
- fixture smoke test with a fake `ssh` executable

No VPS host, SSH key, token, or personal runtime path is stored in Git.

## Configuration

Set these in a local `.env` or shell profile. `.env` is ignored by Git.

Built-in targets:

```text
fake = fake adapter
openclaw = openclaw-cli adapter using XOB_OPENCLAW_*
```

```bash
export XOB_OPENCLAW_SSH_TARGET='<user>@<host>'
export XOB_OPENCLAW_SSH_KEY='/absolute/path/to/private/key'
export XOB_OPENCLAW_SSH_KNOWN_HOSTS='/absolute/path/to/known_hosts'
```

If the Bridge runs on the same VPS as OpenClaw, skip SSH and run the local CLI:

```bash
export XOB_OPENCLAW_SSH_TARGET='local'
```

Add another routable target by mapping a target name to an adapter kind and an
environment-variable prefix:

```bash
export XOB_AGENT_TARGETS='hermes=openclaw-cli:XOB_HERMES'
export XOB_HERMES_SSH_TARGET='<user>@<hermes-host>'
export XOB_HERMES_SSH_KEY='/absolute/path/to/hermes/key'
```

For a LAN Hermes Agent host with a local `hermes` CLI, route the same target
through `hermes-cli`:

```bash
export XOB_AGENT_TARGETS='hermes=hermes-cli:XOB_HERMES'
export XOB_HERMES_SSH_TARGET='ubuntu@192.168.110.30'
export XOB_HERMES_SSH_KEY="$HOME/.ssh/xob_hermes_lan_ed25519"
export XOB_HERMES_CLI_BIN='/usr/local/bin/hermes'
export XOB_HERMES_ENABLE_COMMANDS=1
```

For a same-host test:

```bash
export XOB_AGENT_TARGETS='hermes=hermes-cli:XOB_HERMES'
export XOB_HERMES_SSH_TARGET='local'
export XOB_HERMES_CLI_BIN='hermes'
```

The board-side `xob.default_target` can then be set to `openclaw`, `hermes`, or
any other target name present in `XOB_AGENT_TARGETS`. `openclaw-cli` covers
OpenClaw-compatible CLI/SSH targets. `hermes-cli` covers a host-local Hermes
Agent CLI by running `hermes -z <prompt>` over SSH.

See `docs/PHASE3_HERMES_LAN_CLI.md` for the current LAN Hermes branch target.

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
