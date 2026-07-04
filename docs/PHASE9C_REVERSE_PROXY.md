# Phase 9C Reverse Proxy

Phase 9C adds a minimal nginx TLS reverse proxy sample for a public Bridge host.

## Result

The repository includes:

```text
deploy/nginx/xob-bridge.conf
deploy/systemd/xob-bridge-localhost.conf
```

`xob-bridge.conf` terminates TLS on nginx and proxies to:

```text
http://127.0.0.1:8788
```

`xob-bridge-localhost.conf` is a systemd override that makes the Bridge listen
only on localhost when nginx is in front. It keeps `--require-device-token` and
`--disable-command-route`.

## Install Sketch

On the reachable host, after TLS certificates exist:

```sh
sudo mkdir -p /etc/systemd/system/xob-bridge.service.d
sudo cp deploy/systemd/xob-bridge-localhost.conf /etc/systemd/system/xob-bridge.service.d/localhost.conf
sudo cp deploy/nginx/xob-bridge.conf /etc/nginx/sites-available/xob-bridge.conf
sudo ln -sf /etc/nginx/sites-available/xob-bridge.conf /etc/nginx/sites-enabled/xob-bridge.conf
sudo nginx -t
sudo systemctl daemon-reload
sudo systemctl restart xob-bridge nginx
python3 scripts/check_bridge_health.py https://voice.example.com
```

Then configure the board `bridge_url` to:

```text
https://voice.example.com
```

The nginx sample exposes only `/healthz` and `/device/`. It does not proxy the
generic `/command` route to the public internet.

## Boundaries

- `voice.example.com` is a placeholder.
- No certificate automation is included.
- No WebSocket tuning beyond keeping HTTP/1.1 proxying enabled.
- Provider secrets still belong in host environment or systemd overrides, not
  in nginx config.
