from __future__ import annotations

import base64
import hashlib
import json
import os
import socket
import sqlite3
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from xiaozhi_openclaw_bridge.server import build_server  # noqa: E402

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def main() -> None:
    old_fake_text = os.environ.get("XOB_FAKE_ASR_TEXT")
    os.environ["XOB_FAKE_ASR_TEXT"] = "小元测试"
    with tempfile.TemporaryDirectory() as tmp:
        db = Path(tmp) / "bridge.sqlite3"
        server = build_server("127.0.0.1", 0, db, require_device_token=True)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            host, port = server.server_address
            _wait_for_port(host, port)
            payload, messages = _websocket_session(host, port)
            assert payload["type"] == "hello"
            assert payload["transport"] == "websocket"
            assert payload["session_id"]
            assert payload["audio_params"] == {
                "format": "opus",
                "sample_rate": 24000,
                "channels": 1,
                "frame_duration": 60,
            }
            with sqlite3.connect(db) as conn:
                pairing = conn.execute(
                    "SELECT token_hash, capabilities_json FROM device_pairings WHERE device_id = ?",
                    ("ws-device",),
                ).fetchone()
                events = conn.execute(
                    "SELECT type, payload_json FROM session_events WHERE session_id = ? ORDER BY seq",
                    (payload["session_id"],),
                ).fetchall()
            assert pairing is not None
            assert pairing[0].startswith("sha256:")
            assert "ws-token" not in pairing[0]
            assert json.loads(pairing[1]) == ["websocket", "audio_in", "audio_out"]
            assert [row[0] for row in events] == [
                "device.ws.hello",
                "device.ws.control",
                "device.ws.control",
                "device.ws.audio",
                "command.received",
                "backend.response",
                "device.result",
            ]
            event = json.loads(events[0][1])
            assert event["transport"] == "websocket"
            assert event["audio_params"]["format"] == "opus"
            assert [message["type"] for message in messages] == ["stt", "tts", "tts", "tts"]
            assert messages[0]["text"] == "小元测试"
            assert [message.get("state") for message in messages[1:]] == ["start", "sentence_start", "stop"]
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)
    if old_fake_text is None:
        os.environ.pop("XOB_FAKE_ASR_TEXT", None)
    else:
        os.environ["XOB_FAKE_ASR_TEXT"] = old_fake_text
    print("check_xiaozhi_ws_handshake ok")


def _wait_for_port(host: str, port: int) -> None:
    for _ in range(50):
        try:
            with socket.create_connection((host, port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("server did not become reachable")


def _websocket_session(host: str, port: int) -> tuple[dict[str, object], list[dict[str, object]]]:
    key = base64.b64encode(os.urandom(16)).decode()
    request = (
        "GET /device/ws?target=fake HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Authorization: Bearer ws-token\r\n"
        "Protocol-Version: 1\r\n"
        "Device-Id: ws-device\r\n"
        "Client-Id: ws-client\r\n"
        "\r\n"
    ).encode()
    hello = {
        "type": "hello",
        "version": 1,
        "features": {"mcp": True},
        "transport": "websocket",
        "audio_params": {
            "format": "opus",
            "sample_rate": 16000,
            "channels": 1,
            "frame_duration": 60,
        },
    }
    with socket.create_connection((host, port), timeout=2) as sock:
        sock.sendall(request)
        response = _read_until(sock, b"\r\n\r\n")
        assert b" 101 " in response, response.decode(errors="replace")
        expected_accept = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
        assert f"Sec-WebSocket-Accept: {expected_accept}".encode() in response
        sock.sendall(_masked_json_frame(hello))
        opcode, data = _read_frame(sock)
        assert opcode == 1
        hello_payload = json.loads(data.decode())
        session_id = hello_payload["session_id"]
        sock.sendall(_masked_json_frame({"session_id": session_id, "type": "listen", "state": "start", "mode": "manual"}))
        sock.sendall(_masked_binary_frame(b"\0" * 320))
        sock.sendall(_masked_json_frame({"session_id": session_id, "type": "listen", "state": "stop"}))
        messages = []
        for _ in range(4):
            opcode, data = _read_frame(sock)
            assert opcode == 1
            messages.append(json.loads(data.decode()))
        sock.sendall(_masked_frame(8, b""))
    return hello_payload, messages


def _read_until(sock: socket.socket, marker: bytes) -> bytes:
    data = b""
    while marker not in data:
        chunk = sock.recv(4096)
        if not chunk:
            break
        data += chunk
    return data


def _masked_json_frame(payload: dict[str, object]) -> bytes:
    return _masked_frame(1, json.dumps(payload).encode())


def _masked_binary_frame(payload: bytes) -> bytes:
    return _masked_frame(2, payload)


def _masked_frame(opcode: int, payload: bytes) -> bytes:
    mask = os.urandom(4)
    masked = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
    if len(payload) < 126:
        header = bytes([0x80 | opcode, 0x80 | len(payload)])
    else:
        header = bytes([0x80 | opcode, 0x80 | 126]) + len(payload).to_bytes(2, "big")
    return header + mask + masked


def _read_frame(sock: socket.socket) -> tuple[int, bytes]:
    header = _recv_exact(sock, 2)
    first, second = header
    length = second & 0x7F
    if length == 126:
        length = int.from_bytes(_recv_exact(sock, 2), "big")
    elif length == 127:
        length = int.from_bytes(_recv_exact(sock, 8), "big")
    return first & 0x0F, _recv_exact(sock, length)


def _recv_exact(sock: socket.socket, length: int) -> bytes:
    data = b""
    while len(data) < length:
        chunk = sock.recv(length - len(data))
        if not chunk:
            raise RuntimeError("socket closed")
        data += chunk
    return data


if __name__ == "__main__":
    main()
