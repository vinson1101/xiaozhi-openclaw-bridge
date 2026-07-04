from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
UNIT = ROOT / "deploy" / "systemd" / "xob-bridge.service"
LOCALHOST_OVERRIDE = ROOT / "deploy" / "systemd" / "xob-bridge-localhost.conf"
NGINX = ROOT / "deploy" / "nginx" / "xob-bridge.conf"
HEALTH_CHECK = ROOT / "scripts" / "check_bridge_health.py"


def main() -> None:
    text = UNIT.read_text()
    assert HEALTH_CHECK.exists(), "missing health check script"
    assert LOCALHOST_OVERRIDE.exists(), "missing localhost systemd override"
    assert NGINX.exists(), "missing nginx reverse proxy sample"
    for token in [
        "User=xob",
        "PYTHONPATH=/opt/xiaozhi-openclaw-bridge/src",
        "--host 0.0.0.0",
        "--port 8788",
        "--db /var/lib/xob-bridge/bridge.sqlite3",
        "--require-device-token",
        "Restart=on-failure",
    ]:
        assert token in text, f"missing {token}"
    assert "Bearer " not in text, "service unit must not contain bearer secrets"
    assert "device_token=" not in text, "service unit must not contain raw device tokens"
    override = LOCALHOST_OVERRIDE.read_text()
    assert "--host 127.0.0.1" in override, "localhost override must bind Bridge locally"
    assert "--require-device-token" in override, "localhost override must require device tokens"
    nginx = NGINX.read_text()
    assert "proxy_pass http://127.0.0.1:8788;" in nginx, "nginx must proxy to local Bridge"
    assert "location /device/" in nginx, "nginx must expose device API"
    assert "location / {" in nginx and "return 404;" in nginx, "nginx must not expose all routes"
    assert "ssl_certificate" in nginx, "nginx sample must terminate TLS"
    print("check_deploy_unit ok")


if __name__ == "__main__":
    main()
