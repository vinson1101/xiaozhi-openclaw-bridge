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
    FW / "components" / "vbota" / "CMakeLists.txt",
    FW / "components" / "vbota" / "vb_ota.h",
    FW / "components" / "vbota" / "libesp32c3.a",
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
    for token in ["nvs_flash_init", "bridge_url", "device_token", "default_target", "wifi_ssid", "post_device_hello", "post_device_command", "post_device_audio_probe", "probe_xiaozhi_websocket", "run_vb6824_uart_probe", "start_vb6824_wake_task", "vb6824_is_wake_command", "start_vb6824_ota_code", "jl_ota_start", "jl_ondata", "audio_upload", "websocket", "xob_eyes_frame", "xob_screen_render_avatar", "xob_start_ap_provisioning", "xob_run_serial_provisioning", "XOB_BUTTON_LISTEN_GPIO", "XOB_VB_TALK_AUTO_MAX_FRAMES", "vb6824_voice_stop_requested", "\\\"mode\\\":\\\"%s", "esp32c3"]:
        assert token in main_c or token in (FW / "sdkconfig.defaults").read_text(), f"missing {token}"
    for token in ["driver/gpio.h", "driver/uart.h", "driver/usb_serial_jtag.h", "GPIO_NUM_7", "GPIO_NUM_8", "GPIO_NUM_9", "button_task", "serial_command_task", "小元", ":config", ":setup", ":status", ":target", ":vb", ":vb-ota", ":vb-talk", ":tone", ":voice", ":audio", ":ws", ":talk"]:
        assert token in main_c, f"missing {token}"
    assert "nvs_flash_erase" not in main_c, "firmware must not erase stock NVS automatically"
    eyes_c = (FW / "main" / "eyes.c").read_text()
    for token in ["XOB_EYES_IDLE", "XOB_EYES_LISTENING", "XOB_EYES_THINKING", "XOB_EYES_SPEAKING", "XOB_EYES_ERROR"]:
        assert token in eyes_c, f"missing {token}"
    screen_c = (FW / "main" / "screen.c").read_text()
    for token in ["XOB_SCREEN_WIDTH", "XOB_SCREEN_HEIGHT", "XOB_RGB565_WHITE", "XOB_RGB565_BLACK", "xob_screen_render_avatar"]:
        assert token in screen_c or token in (FW / "main" / "screen.h").read_text(), f"missing {token}"
    provisioning_c = (FW / "main" / "provisioning.c").read_text()
    for token in ["NVS_READWRITE", "nvs_set_str", "nvs_commit", "bridge_url", "device_token", "default_target", "wifi_ssid", "wifi_password", "esp_http_server", "httpd_start", "WIFI_MODE_APSTA", "esp_restart", "keep_existing_if_empty", "Press Enter to keep"]:
        assert token in provisioning_c, f"missing {token}"
    build_script = (ROOT / "scripts" / "build_firmware.sh").read_text()
    for token in ["idf.py set-target esp32c3", "idf.py build"]:
        assert token in build_script, f"missing {token}"
    main_cmake = (FW / "main" / "CMakeLists.txt").read_text()
    for token in ["esp_driver_uart", "vbota"]:
        assert token in main_cmake, f"missing {token}"
    lcd_c = (FW / "main" / "lcd.c").read_text()
    assert "clear dialog" not in lcd_c, "dialog text must not clear over the mouth"
    assert "draw_text_line(text, x, 212" in lcd_c, "dialog text must stay below the mouth"
    assert "draw marquee tail" in lcd_c, "long dialog text must scroll on one line"
    assert "status_x = (int16_t)(XOB_SCREEN_WIDTH - text_width(status_text) - 8)" in lcd_c, "status must be right aligned"
    assert "ascii_glyph" in lcd_c, "status text must use a stable built-in bitmap font"
    assert "display_char" in lcd_c, "non-ASCII text must be normalized instead of rendered as broken glyphs"
    assert "draw_ascii_char" in lcd_c, "dialog text should avoid unstable LVGL glyph rendering"
    assert "text_is_displayable" in lcd_c, "non-ASCII dialog subtitles must not render as question marks"
    assert "*p >= 0x80" in main_c, "non-ASCII text must not keep triggering invisible marquee refresh"
    assert "eye_tick = state == XOB_EYES_IDLE ? tick_ms : 0" in main_c, "thinking/speaking animation should not force full-screen redraws"
    assert "XOB_VB_PLAY_FRAME_DELAY_MS 10" in main_c and "vTaskDelayUntil(&last_time" in main_c, "VB6824 playback must match stock 320-byte/10ms pacing"
    assert "XOB_VB_UART_TX_BUFFER_BYTES 4096" in main_c, "VB6824 playback needs room to queue UART TX frames"
    assert "xRingbufferCreate(XOB_VB_PLAY_QUEUE_BYTES" in main_c and "vb6824_playback_task" in main_c, "TTS playback must use a queue instead of blocking WebSocket receive"
    assert "static int applied_volume" in main_c and "applied_volume != volume" in main_c, "TTS playback must not resend VB6824 volume before every audio chunk"
    assert "set_avatar_state(XOB_EYES_LISTENING, avatar_wifi_status, XOB_SCREEN_STATUS_PENDING)" in main_c, "voice session must show bridge reconnect pending"
    assert "set_avatar_state(XOB_EYES_IDLE, avatar_wifi_status, XOB_SCREEN_STATUS_OK)" in main_c, "successful voice session must clear stale bridge error"
    assert "show_tts_speaking_once(&tts_refresh_paused)" in main_c, "TTS must draw SPEAKING before pausing display refresh"
    assert "play_test_tone" in main_c and "local_volume = 100" in main_c, "local VB6824 playback needs a max-volume diagnostic tone"
    assert "XOB_WS_RECV_TIMEOUT_MS 90000" in main_c, "WebSocket receive timeout must not leave the board stuck for minutes"
    assert "status: volume=%d" in main_c, "serial status must report current volume"
    assert "XOB_WS_MESSAGE_BUFFER_BYTES 16384" in main_c, "WebSocket receive buffer must fit ESP32-C3 RAM"
    assert "play_tts_pcm_frame" in main_c and "vb6824_play_pcm(pcm, pcm_len" in main_c, "TTS audio frames must enter playback as they arrive"
    assert "avatar_refresh_paused" in main_c and "pdMS_TO_TICKS(250)" in main_c, "display refresh must pause through TTS playback"
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
