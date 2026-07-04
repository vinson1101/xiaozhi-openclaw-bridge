from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
UNIT = ROOT / "deploy" / "systemd" / "xob-bridge.service"


def main() -> None:
    text = UNIT.read_text()
    for token in [
        "User=xob",
        "PYTHONPATH=/opt/xiaozhi-openclaw-bridge/src",
        "--host 0.0.0.0",
        "--port 8788",
        "--db /var/lib/xob-bridge/bridge.sqlite3",
        "Restart=on-failure",
    ]:
        assert token in text, f"missing {token}"
    assert "token" not in text.lower(), "service unit must not contain secrets"
    print("check_deploy_unit ok")


if __name__ == "__main__":
    main()
