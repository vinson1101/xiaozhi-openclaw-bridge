# Phase 9A Systemd Service

Phase 9A adds the smallest deployable Bridge service template for a host the
board can reach.

## Result

The repository includes:

```text
deploy/systemd/xob-bridge.service
```

The unit runs:

```text
python3 -m xiaozhi_openclaw_bridge.server --host 0.0.0.0 --port 8788 --db /var/lib/xob-bridge/bridge.sqlite3 --require-device-token
```

This is an HTTP-only development service. It is useful for a LAN host or a
temporary VPS test where the board must reach Bridge before the audio stack is
implemented.

## Install Sketch

On a reachable Linux host:

```sh
sudo useradd --system --home /nonexistent --shell /usr/sbin/nologin xob
sudo mkdir -p /opt/xiaozhi-openclaw-bridge
sudo rsync -a --delete ./ /opt/xiaozhi-openclaw-bridge/
sudo cp /opt/xiaozhi-openclaw-bridge/deploy/systemd/xob-bridge.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now xob-bridge
python3 /opt/xiaozhi-openclaw-bridge/scripts/check_bridge_health.py http://127.0.0.1:8788
```

Then configure the board `bridge_url` to the reachable host URL, for example:

```text
http://<reachable-host>:8788
```

Also configure a non-empty `device_token`; deployment mode rejects device hello
without a Bearer token.

Before changing the board, verify that same URL from a host on the same network
path:

```sh
python3 scripts/check_bridge_health.py http://<reachable-host>:8788
```

## Boundaries

- No TLS or reverse proxy yet.
- No public-internet hardening yet.
- No credentials or API keys in the unit.
- Device token enforcement is enabled. Token rotation is an offline host
  operation through `scripts/rotate_device_credential.py`.
- Keep OpenClaw, Hermas, Zebra, and provider secrets in host environment or
  service overrides, not in Git.

## Next

Use this only to unblock real `/device/hello`. After that, add reverse proxy,
TLS, token rotation, and provider-specific secret handling before exposing it
as a real public service.
