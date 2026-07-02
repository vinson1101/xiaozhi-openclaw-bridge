from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / "firmware" / "esp32c3"
REQUIRED = [
    FW / "CMakeLists.txt",
    FW / "main" / "CMakeLists.txt",
    FW / "main" / "main.c",
    FW / "main" / "eyes.c",
    FW / "main" / "eyes.h",
    FW / "sdkconfig.defaults",
    FW / "partitions.csv",
]


def main() -> None:
    missing = [str(path.relative_to(ROOT)) for path in REQUIRED if not path.exists()]
    assert not missing, f"missing firmware files: {', '.join(missing)}"
    main_c = (FW / "main" / "main.c").read_text()
    for token in ["nvs_flash_init", "bridge_url", "device_token", "wifi_ssid", "post_device_hello", "xob_eyes_frame", "esp32c3"]:
        assert token in main_c or token in (FW / "sdkconfig.defaults").read_text(), f"missing {token}"
    eyes_c = (FW / "main" / "eyes.c").read_text()
    for token in ["XOB_EYES_IDLE", "XOB_EYES_LISTENING", "XOB_EYES_THINKING", "XOB_EYES_SPEAKING", "XOB_EYES_ERROR"]:
        assert token in eyes_c, f"missing {token}"
    print("check_firmware_skeleton ok")


if __name__ == "__main__":
    main()
