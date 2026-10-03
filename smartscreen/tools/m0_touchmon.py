#!/usr/bin/env python3
"""M0 diagnostic: watch the touchscreen and survive replugs.

Prints every raw ABS/KEY event, re-discovering the input node when the device
disappears and comes back (it usually returns as a different eventN).  Used to
tell "the panel never reports touches" apart from "the driver stopped polling
the interrupt endpoint at some point".
"""

from __future__ import annotations

import os
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ccdisp.touch import (  # noqa: E402
    ABS_X, ABS_Y, BTN_TOUCH, EV_ABS, EV_KEY, EV_SYN, EVENT_SIZE,
    DEFAULT_TOUCH_NAME, TouchNotFound, find_touch_device,
)

_EVENT = struct.Struct("@llHHi")
ABS_PRESSURE = 0x18

NAMES = {
    (EV_ABS, ABS_X): "ABS_X",
    (EV_ABS, ABS_Y): "ABS_Y",
    (EV_ABS, ABS_PRESSURE): "ABS_PRESSURE",
    (EV_KEY, BTN_TOUCH): "BTN_TOUCH",
    (EV_SYN, 0): "SYN_REPORT",
}


def monitor(path: str) -> None:
    fd = os.open(path, os.O_RDONLY)
    print(f"[{time.strftime('%H:%M:%S')}] reading {path} — touch the panel")
    count = 0
    try:
        while True:
            data = os.read(fd, EVENT_SIZE * 32)
            if not data:
                break
            for off in range(0, len(data) - EVENT_SIZE + 1, EVENT_SIZE):
                sec, usec, etype, code, value = _EVENT.unpack_from(data, off)
                label = NAMES.get((etype, code), f"type={etype} code={code}")
                if etype == EV_SYN:
                    print(f"  {label}")
                else:
                    print(f"  {label:<13} = {value}")
                count += 1
    except OSError as exc:
        print(f"[{time.strftime('%H:%M:%S')}] device went away ({exc.strerror}), "
              f"{count} events seen")
    finally:
        os.close(fd)


def main() -> int:
    sys.stdout.reconfigure(line_buffering=True)
    deadline = time.monotonic() + 300
    known: str | None = None
    while time.monotonic() < deadline:
        try:
            path = find_touch_device(DEFAULT_TOUCH_NAME)
        except TouchNotFound:
            if known is not None:
                print(f"[{time.strftime('%H:%M:%S')}] waiting for the panel to come back")
                known = None
            time.sleep(0.5)
            continue
        if path != known:
            known = path
        try:
            monitor(path)
        except PermissionError:
            print(f"{path}: no access — is the udev rule installed?")
            return 1
        time.sleep(0.3)
    print("done (5 min)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\ninterrupted")
