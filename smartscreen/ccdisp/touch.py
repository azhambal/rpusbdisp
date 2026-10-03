"""Touchscreen reader for the RoboPeak USB display.

The kernel driver registers a plain single-touch input device reporting ABS_X,
ABS_Y, ABS_PRESSURE and BTN_TOUCH, so we can read /dev/input/eventN directly
and skip the python-evdev dependency.
"""

from __future__ import annotations

import os
import selectors
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator

DEFAULT_TOUCH_NAME = "RoboPeakUSBDisplayTS"

# struct input_event: struct timeval (two longs), __u16 type, __u16 code, __s32 value.
# The format must be native ("@"): under "=" Python uses standard sizes where a
# long is 4 bytes, which silently yields a 16-byte event and shreds the stream.
_EVENT = struct.Struct("@llHHi")
EVENT_SIZE = _EVENT.size
assert EVENT_SIZE in (16, 24), f"unexpected input_event size {EVENT_SIZE}"

EV_SYN, EV_KEY, EV_ABS = 0x00, 0x01, 0x03
ABS_X, ABS_Y = 0x00, 0x01
BTN_TOUCH = 0x14A


class TouchNotFound(Exception):
    pass


@dataclass(frozen=True)
class TouchEvent:
    kind: str  # "down" | "move" | "up"
    x: int
    y: int
    time: float


def find_touch_device(name: str = DEFAULT_TOUCH_NAME) -> str:
    for entry in sorted(Path("/sys/class/input").glob("event*")):
        try:
            if (entry / "device" / "name").read_text().strip() == name:
                return f"/dev/input/{entry.name}"
        except OSError:
            continue
    raise TouchNotFound(f"no input device named {name!r} under /sys/class/input")


class TouchReader:
    """Turns the raw event stream into down/move/up events."""

    def __init__(self, name: str = DEFAULT_TOUCH_NAME, path: str | None = None):
        self.path = path or find_touch_device(name)
        self._fd = os.open(self.path, os.O_RDONLY | os.O_NONBLOCK)
        self._sel = selectors.DefaultSelector()
        self._sel.register(self._fd, selectors.EVENT_READ)
        self._x = 0
        self._y = 0
        self._down = False
        self._buf = b""

    def fileno(self) -> int:
        return self._fd

    def wait(self, timeout: float | None = None) -> bool:
        """Block until events are pending. False on timeout."""
        return bool(self._sel.select(timeout))

    def read(self) -> Iterator[TouchEvent]:
        """Drain whatever is pending and yield synthesised touch events."""
        try:
            chunk = os.read(self._fd, EVENT_SIZE * 64)
        except BlockingIOError:
            return
        self._buf += chunk
        pending_down: bool | None = None
        moved = False
        stamp = 0.0

        while len(self._buf) >= EVENT_SIZE:
            sec, usec, etype, code, value = _EVENT.unpack(self._buf[:EVENT_SIZE])
            self._buf = self._buf[EVENT_SIZE:]
            stamp = sec + usec / 1_000_000

            if etype == EV_ABS:
                if code == ABS_X and value != self._x:
                    self._x, moved = value, True
                elif code == ABS_Y and value != self._y:
                    self._y, moved = value, True
            elif etype == EV_KEY and code == BTN_TOUCH:
                pending_down = bool(value)
            elif etype == EV_SYN:
                if pending_down is True and not self._down:
                    self._down = True
                    yield TouchEvent("down", self._x, self._y, stamp)
                elif pending_down is False and self._down:
                    self._down = False
                    yield TouchEvent("up", self._x, self._y, stamp)
                elif moved and self._down:
                    yield TouchEvent("move", self._x, self._y, stamp)
                pending_down, moved = None, False

    def close(self) -> None:
        if getattr(self, "_fd", None) is not None:
            self._sel.close()
            os.close(self._fd)
            self._fd = None

    def __enter__(self) -> "TouchReader":
        return self

    def __exit__(self, *exc) -> None:
        self.close()
