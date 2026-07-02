from __future__ import annotations

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


def adapter_for(target: str) -> FakeAdapter:
    if target == "fake":
        return FakeAdapter()
    raise ValueError(f"unsupported target: {target}")
