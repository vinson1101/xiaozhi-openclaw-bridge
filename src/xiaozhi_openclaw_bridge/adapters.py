from __future__ import annotations

import json
import os
import shlex
import subprocess
from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True)
class AgentRequest:
    session_id: str
    target: str
    user_text: str
    context: dict[str, Any]


@dataclass(frozen=True)
class AgentResponse:
    status: str
    text: str
    summary: str
    artifacts: list[dict[str, Any]]


@dataclass(frozen=True)
class OpenClawConfig:
    ssh_target: str
    ssh_bin: str
    ssh_key: str | None
    known_hosts: str | None
    agent: str | None
    enable_commands: bool
    session_prefix: str
    connect_timeout: int
    command_timeout: int

    @classmethod
    def from_env(cls) -> "OpenClawConfig":
        return cls(
            ssh_target=os.environ.get("XOB_OPENCLAW_SSH_TARGET", "").strip(),
            ssh_bin=os.environ.get("XOB_OPENCLAW_SSH_BIN", "ssh").strip() or "ssh",
            ssh_key=_blank_to_none(os.environ.get("XOB_OPENCLAW_SSH_KEY")),
            known_hosts=_blank_to_none(os.environ.get("XOB_OPENCLAW_SSH_KNOWN_HOSTS")),
            agent=_blank_to_none(os.environ.get("XOB_OPENCLAW_AGENT")),
            enable_commands=_truthy(os.environ.get("XOB_OPENCLAW_ENABLE_COMMANDS")),
            session_prefix=os.environ.get("XOB_OPENCLAW_SESSION_PREFIX", "xiaozhi-bridge").strip()
            or "xiaozhi-bridge",
            connect_timeout=_int_env("XOB_OPENCLAW_CONNECT_TIMEOUT", 10),
            command_timeout=_int_env("XOB_OPENCLAW_TIMEOUT", 600),
        )


class FakeAdapter:
    target = "fake"

    def run(self, request: AgentRequest) -> AgentResponse:
        text = f"收到：{request.user_text}\nFake 后端已完成文本链路验证。"
        return AgentResponse(
            status="done",
            text=text,
            summary=f"fake handled {len(request.user_text)} chars",
            artifacts=[],
        )


class OpenClawSshAdapter:
    target = "openclaw"

    def __init__(self, config: OpenClawConfig | None = None) -> None:
        self.config = config or OpenClawConfig.from_env()

    def run(self, request: AgentRequest) -> AgentResponse:
        if not self.config.ssh_target:
            return AgentResponse(
                status="error",
                text="OpenClaw SSH 未配置：请设置 XOB_OPENCLAW_SSH_TARGET。",
                summary="missing XOB_OPENCLAW_SSH_TARGET",
                artifacts=[],
            )
        if not self.config.enable_commands:
            return self._health_only()
        return self._agent_turn(request)

    def _health_only(self) -> AgentResponse:
        result = self._run_openclaw(["health", "--json", "--timeout", "5000"], timeout=15)
        if result.returncode != 0:
            return _process_error_response("OpenClaw health 检查失败", result)

        data = _parse_json_payload(result.stdout)
        if isinstance(data, dict):
            ok = data.get("ok") is True
            status_text = str(data.get("status") or ("live" if ok else "unknown"))
            ok = ok or status_text == "live"
        else:
            status_text = "unknown"
            ok = False

        return AgentResponse(
            status="done" if ok else "error",
            text=f"OpenClaw gateway 状态：{status_text}。命令转发未开启。",
            summary=f"openclaw health {status_text}",
            artifacts=[],
        )

    def _agent_turn(self, request: AgentRequest) -> AgentResponse:
        session_key = f"{self.config.session_prefix}:{request.session_id}"
        args = [
            "agent",
            "--json",
            "--message",
            request.user_text,
            "--session-key",
            session_key,
            "--timeout",
            str(self.config.command_timeout),
        ]
        if self.config.agent:
            args.extend(["--agent", self.config.agent])

        result = self._run_openclaw(args, timeout=self.config.command_timeout + 15)
        if result.returncode != 0:
            return _process_error_response("OpenClaw agent 调用失败", result)

        data = _parse_json_payload(result.stdout)
        text = _extract_text(data) or _truncate(result.stdout.strip(), 1200)
        status = _extract_status(data) or "done"
        return AgentResponse(
            status=status,
            text=text,
            summary=f"openclaw agent {status}",
            artifacts=[{"type": "openclaw_agent", "session_key": session_key}],
        )

    def _run_openclaw(self, args: list[str], timeout: int) -> subprocess.CompletedProcess[str]:
        argv = [
            self.config.ssh_bin,
            "-o",
            "BatchMode=yes",
            "-o",
            f"ConnectTimeout={self.config.connect_timeout}",
        ]
        if self.config.known_hosts:
            argv.extend(
                [
                    "-o",
                    f"UserKnownHostsFile={self.config.known_hosts}",
                    "-o",
                    "StrictHostKeyChecking=yes",
                ]
            )
        if self.config.ssh_key:
            argv.extend(["-i", self.config.ssh_key])
        remote_command = " ".join(shlex.quote(part) for part in ["openclaw", *args])
        argv.extend([self.config.ssh_target, remote_command])
        try:
            return subprocess.run(
                argv,
                text=True,
                capture_output=True,
                timeout=timeout,
                check=False,
            )
        except subprocess.TimeoutExpired as exc:
            return subprocess.CompletedProcess(
                argv,
                124,
                stdout=_coerce_text(exc.stdout),
                stderr=_coerce_text(exc.stderr) + "\ncommand timed out",
            )


def adapter_for(target: str) -> Any:
    if target == "fake":
        return FakeAdapter()
    if target == "openclaw":
        return OpenClawSshAdapter()
    raise ValueError(f"unsupported target: {target}")


def _truthy(value: str | None) -> bool:
    return (value or "").strip().lower() in {"1", "true", "yes", "on"}


def _blank_to_none(value: str | None) -> str | None:
    stripped = (value or "").strip()
    return stripped or None


def _int_env(name: str, default: int) -> int:
    raw = os.environ.get(name, "").strip()
    if not raw:
        return default
    try:
        value = int(raw)
    except ValueError:
        return default
    return value if value > 0 else default


def _parse_json_payload(text: str) -> Any:
    stripped = text.strip()
    if not stripped:
        return None
    try:
        return json.loads(stripped)
    except json.JSONDecodeError:
        start = stripped.find("{")
        end = stripped.rfind("}")
        if start >= 0 and end > start:
            try:
                return json.loads(stripped[start : end + 1])
            except json.JSONDecodeError:
                return None
    return None


def _extract_status(data: Any) -> str | None:
    if not isinstance(data, dict):
        return None
    if _has_needs_approval(data):
        return "needs_approval"
    for key in ("status", "state"):
        value = data.get(key)
        if isinstance(value, str) and value:
            return value
    return None


def _extract_text(data: Any) -> str | None:
    if isinstance(data, str):
        return data
    if not isinstance(data, dict):
        return None
    for key in ("text", "reply", "response", "message", "content", "output", "answer"):
        value = data.get(key)
        if isinstance(value, str) and value.strip():
            return value.strip()
        nested = _extract_text(value)
        if nested:
            return nested
    for key in ("result", "data", "turn", "assistant"):
        nested = _extract_text(data.get(key))
        if nested:
            return nested
    return None


def _process_error_response(prefix: str, result: subprocess.CompletedProcess[str]) -> AgentResponse:
    detail = _truncate((result.stderr or result.stdout or "").strip(), 1200)
    return AgentResponse(
        status="error",
        text=f"{prefix}：{detail or 'no output'}",
        summary=f"openclaw command exited {result.returncode}",
        artifacts=[],
    )


def _truncate(text: str, limit: int) -> str:
    if len(text) <= limit:
        return text
    return text[: limit - 3] + "..."


def _has_needs_approval(data: Any) -> bool:
    if isinstance(data, dict):
        if data.get("needs_approval") is True:
            return True
        return any(_has_needs_approval(value) for value in data.values())
    if isinstance(data, list):
        return any(_has_needs_approval(value) for value in data)
    return False


def _coerce_text(value: Any) -> str:
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode("utf-8", "replace")
    return str(value)
