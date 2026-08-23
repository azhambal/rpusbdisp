"""python3 -m ccdisp.clock — the panel as a clock and nothing else.

The same face ccdispd shows when no card is waiting, for machines that do not
run Claude Code hooks.  Only one process can own the panel, so run this or the
daemon, never both.  A tap asks for a fresh weather reading.

  python3 -m ccdisp.clock                # drive the panel
  python3 -m ccdisp.clock --png out.png  # one frame to a file, no panel needed
"""

from __future__ import annotations

import argparse
import logging
import os
import selectors
import signal
import sys
import time
from datetime import datetime

from .clockface import Face
from .config import Config
from .fb import FbNotFound, Framebuffer
from .touch import TouchNotFound, TouchReader

log = logging.getLogger("ccdisp.clock")


class ClockApp:
    def __init__(self, cfg: Config) -> None:
        self.cfg = cfg
        self.fb = Framebuffer()
        self.face = Face(self.fb.size, cfg)
        self.sel = selectors.DefaultSelector()
        self.touch: TouchReader | None = None
        try:
            self.touch = TouchReader()
            self.sel.register(self.touch.fileno(), selectors.EVENT_READ)
        except (TouchNotFound, PermissionError, OSError) as exc:
            # a clock without touch is still a clock; only the tap-to-refresh
            # shortcut is lost
            log.info("no touchscreen (%s) — running without tap to refresh", exc)

        self._running = True

    def stop(self, *_args) -> None:
        self._running = False

    def run(self) -> None:
        log.info("panel %s, %dx%d", self.fb.info.path, *self.fb.size)
        self.face.start()
        while self._running:
            image = self.face.frame()
            if image is not None:
                self.fb.blit_changed(image)
            timeout = self.face.timeout()
            if self.touch is None:
                time.sleep(timeout)
                continue
            for _key, _mask in self.sel.select(timeout):
                for event in self.touch.read():
                    if event.kind == "down":
                        log.debug("tap -> refreshing weather")
                        self.face.refresh()

    def close(self) -> None:
        self.face.stop()
        try:
            self.fb.fill((0, 0, 0))
        except OSError:
            pass
        self.sel.close()
        if self.touch is not None:
            self.touch.close()
        self.fb.close()


def _render_png(cfg: Config, path: str, wait: float) -> int:
    """Draw one frame to a file — the way to see the face without the hardware."""
    face = Face((320, 240), cfg)
    face.start()
    deadline = time.monotonic() + wait
    while face.weather is not None and face.weather.latest() is None:
        if time.monotonic() >= deadline:
            log.warning("no weather after %.0fs: %s", wait,
                        face.weather.error or "still trying")
            break
        time.sleep(0.2)
    image = face.frame(now=datetime.now())
    face.stop()
    if image is None:
        return 1
    image.save(path)
    print(f"wrote {path}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="ccdisp.clock", description=__doc__)
    parser.add_argument("--png", metavar="FILE",
                        help="render a single frame to FILE and exit")
    parser.add_argument("--wait", type=float, default=15.0,
                        help="seconds --png waits for the first weather reading")
    args = parser.parse_args(argv)

    level = logging.DEBUG if os.environ.get("CCDISP_DEBUG") else logging.INFO
    logging.basicConfig(level=level, format="%(asctime)s %(levelname)s %(message)s")

    cfg = Config.load()
    if args.png:
        return _render_png(cfg, args.png, args.wait)

    try:
        app = ClockApp(cfg)
    except FbNotFound as exc:
        log.error("%s", exc)
        return 1
    except PermissionError as exc:
        log.error("%s — is the udev rule installed?", exc)
        return 1

    signal.signal(signal.SIGTERM, app.stop)
    signal.signal(signal.SIGINT, app.stop)
    try:
        app.run()
    finally:
        app.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
