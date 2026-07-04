from __future__ import annotations

import argparse
import json
from urllib.request import urlopen


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("base_url", help="Bridge base URL, for example http://127.0.0.1:8788")
    parser.add_argument("--timeout", type=float, default=3.0)
    args = parser.parse_args()

    base_url = args.base_url.rstrip("/")
    with urlopen(f"{base_url}/healthz", timeout=args.timeout) as res:
        payload = json.loads(res.read().decode())
    assert res.status == 200, f"unexpected status: {res.status}"
    assert payload == {"status": "ok"}, f"unexpected payload: {payload}"
    print("check_bridge_health ok")


if __name__ == "__main__":
    main()
