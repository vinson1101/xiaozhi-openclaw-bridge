from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / "firmware" / "esp32c3"
REQUIRED = [
    ROOT / "scripts" / "build_firmware.sh",
    FW / "CMakeLists.txt",
    FW / "main" / "CMakeLists.txt",
    FW / "main" / "main.c",
    FW / "main" / "eyes.c",
    FW / "main" / "eyes.h",
    FW / "main" / "screen.c",
    FW / "main" / "screen.h",
    FW / "main" / "provisioning.c",
    FW / "main" / "provisioning.h",
    FW / "sdkconfig.defaults",
    FW / "partitions.csv",
]
PARTITIONS = {
    "nvs": ("data", "nvs", "0x9000", "0x4000"),
    "otadata": ("data", "ota", "0xd000", "0x2000"),
    "phy_init": ("data", "phy", "0xf000", "0x1000"),
    "model": ("data", "0x82", "0x10000", "0xf0000"),
    "ota_0": ("app", "ota_0", "0x100000", "0x380000"),
    "ota_1": ("app", "ota_1", "0x480000", "0x380000"),
}


def main() -> None:
    missing = [str(path.relative_to(ROOT)) for path in REQUIRED if not path.exists()]
    assert not missing, f"missing firmware files: {', '.join(missing)}"
    main_c = (FW / "main" / "main.c").read_text()
    for token in ["nvs_flash_init", "bridge_url", "device_token", "wifi_ssid", "post_device_hello", "xob_eyes_frame", "xob_screen_render_avatar", "xob_start_ap_provisioning", "xob_run_serial_provisioning", "XOB_BUTTON_LISTEN_GPIO", "esp32c3"]:
        assert token in main_c or token in (FW / "sdkconfig.defaults").read_text(), f"missing {token}"
    for token in ["driver/gpio.h", "GPIO_NUM_7", "GPIO_NUM_8", "GPIO_NUM_9", "button_task"]:
        assert token in main_c, f"missing {token}"
    assert "nvs_flash_erase" not in main_c, "firmware must not erase stock NVS automatically"
    eyes_c = (FW / "main" / "eyes.c").read_text()
    for token in ["XOB_EYES_IDLE", "XOB_EYES_LISTENING", "XOB_EYES_THINKING", "XOB_EYES_SPEAKING", "XOB_EYES_ERROR"]:
        assert token in eyes_c, f"missing {token}"
    screen_c = (FW / "main" / "screen.c").read_text()
    for token in ["XOB_SCREEN_WIDTH", "XOB_SCREEN_HEIGHT", "XOB_RGB565_WHITE", "XOB_RGB565_BLACK", "xob_screen_render_avatar"]:
        assert token in screen_c or token in (FW / "main" / "screen.h").read_text(), f"missing {token}"
    provisioning_c = (FW / "main" / "provisioning.c").read_text()
    for token in ["NVS_READWRITE", "nvs_set_str", "nvs_commit", "bridge_url", "device_token", "wifi_ssid", "wifi_password", "esp_http_server", "httpd_start", "WIFI_MODE_APSTA", "esp_restart"]:
        assert token in provisioning_c, f"missing {token}"
    build_script = (ROOT / "scripts" / "build_firmware.sh").read_text()
    for token in ["idf.py set-target esp32c3", "idf.py build"]:
        assert token in build_script, f"missing {token}"
    assert "idf.py flash" not in build_script, "build script must not flash"
    partitions = _read_partitions(FW / "partitions.csv")
    assert partitions == PARTITIONS, "firmware partition table must match stock layout"
    print("check_firmware_skeleton ok")


def _read_partitions(path: Path) -> dict[str, tuple[str, str, str, str]]:
    rows: dict[str, tuple[str, str, str, str]] = {}
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        name, part_type, subtype, offset, size = [item.strip().lower() for item in line.split(",")]
        rows[name] = (part_type, subtype, offset, size)
    return rows


if __name__ == "__main__":
    main()
