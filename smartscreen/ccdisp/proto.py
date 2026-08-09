"""Wire protocol between the hook and the daemon.

One JSON object per line over a Unix socket.  The daemon knows nothing about
Claude Code: it is handed a card with a list of buttons and returns which one
was pressed.  All translation between Claude Code's hook payloads and this
shape lives in the hook.
"""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field
from typing import Any

VERSION = 1

# Answer sources, i.e. how the choice was arrived at.
SRC_TOUCH = "touch"
SRC_TIMEOUT = "timeout"
SRC_CANCEL = "cancel"
SRC_UNAVAILABLE = "unavailable"

# Layouts the renderer knows how to draw.
LAYOUT_PERMISSION = "permission"  # two decisions side by side, optional wide rows
LAYOUT_LIST = "list"              # 2-5 stacked full-width buttons
LAYOUT_NOTICE = "notice"          # no buttons, informational only


@dataclass
class Button:
    id: str
    label: str
    sub: str = ""
    color: str = "neutral"  # green | yellow | red | neutral


@dataclass
class Card:
    id: str
    title: str
    body: str = ""
    detail: str = ""
    kind: str = "permission"
    layout: str = LAYOUT_PERMISSION
    session_label: str = ""
    mode: str = ""
    risk: str = "low"
    buttons: list[Button] = field(default_factory=list)
    default: str = ""
    timeout_ms: int = 110_000

    def to_json(self) -> str:
        d = asdict(self)
        d["v"] = VERSION
        d["type"] = "card"
        return json.dumps(d, ensure_ascii=False)

    @classmethod
    def from_obj(cls, obj: dict[str, Any]) -> "Card":
        data = {k: v for k, v in obj.items() if k not in ("v", "type")}
        data["buttons"] = [Button(**b) for b in obj.get("buttons", [])]
        return cls(**data)


@dataclass
class Answer:
    id: str
    choice: str
    source: str = SRC_TOUCH
    elapsed_ms: int = 0

    def to_json(self) -> str:
        d = asdict(self)
        d["v"] = VERSION
        return json.dumps(d, ensure_ascii=False)

    @classmethod
    def from_obj(cls, obj: dict[str, Any]) -> "Answer":
        return cls(**{k: v for k, v in obj.items() if k != "v"})


def encode(obj: Card | Answer) -> bytes:
    return obj.to_json().encode("utf-8") + b"\n"
