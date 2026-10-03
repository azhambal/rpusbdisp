"""python3 -m ccdisp.clock — the panel as a desk clock you swipe through.

Four pages side by side, StandBy-style; the digital clock with the weather is
home and the panel starts there:

    analog  <-  clock (home)  ->  calendar  ->  sky

Swipe sideways to turn a page.  A tap on the home page asks for a fresh
weather reading.

  python3 -m ccdisp.clock                         # drive the panel
  python3 -m ccdisp.clock --png out.png           # home page to a file, no panel
  python3 -m ccdisp.clock --png out.png --page sky
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

from .analog import AnalogClock
from .clockface import Face
from .config import Config, known_location
from .fb import FbNotFound, Framebuffer
from .monthview import MonthView
from .pager import Page, Pager
from .sky import Sky
from .touch import TouchNotFound, TouchReader

log = logging.getLogger("ccdisp.clock")

PAGE_NAMES = ("analog", "clock", "calendar", "sky")
HOME = PAGE_NAMES.index("clock")


def build_pages(size: tuple[int, int], cfg: Config) -> list[Page]:
    face = Face(size, cfg)

    def locate():
        # the weather service has the freshest answer, including one it just
        # geolocated; without weather, fall back to what is already on disk
        if face.weather is not None and face.weather.location is not None:
            return face.weather.location
        return known_location(cfg)

    return [AnalogClock(size, cfg), face, MonthView(size, cfg), Sky(size, locate)]


class ClockApp:
    def __init__(self, cfg: Config) -> None:
        self.cfg = cfg
        self.fb = Framebuffer()
        self.pager = Pager(self.fb.size, build_pages(self.fb.size, cfg), HOME)
        self.sel = selectors.DefaultSelector()
        self.touch: TouchReader | None = None
        try:
            self.touch = TouchReader()
            self.sel.register(self.touch.fileno(), selectors.EVENT_READ)
        except (TouchNotFound, PermissionError, OSError) as exc:
            # a clock without touch is still a clock; it just stays on home
            log.info("no touchscreen (%s) — running without swipes", exc)

        self._running = True

    def stop(self, *_args) -> None:
        self._running = False

    def run(self) -> None:
        log.info("panel %s, %dx%d", self.fb.info.path, *self.fb.size)
        self.pager.start()
        width, height = self.fb.size
        while self._running:
            image = self.pager.frame()
            if image is not None:
                self.fb.blit_changed(image)
            timeout = self.pager.timeout()
            if self.touch is None:
                time.sleep(timeout)
                continue
            for _key, _mask in self.sel.select(timeout):
                for event in self.touch.read():
                    # the driver reports ABS_X as 0..320, one past the edge
                    x = max(0, min(width - 1, event.x))
                    y = max(0, min(height - 1, event.y))
                    log.debug("touch %s %d,%d", event.kind, x, y)
                    self.pager.touch(event.kind, x, y)

    def close(self) -> None:
        self.pager.stop()
        try:
            self.fb.fill((0, 0, 0))
        except OSError:
            pass
        self.sel.close()
        if self.touch is not None:
            self.touch.close()
        self.fb.close()


def _render_png(cfg: Config, path: str, page_name: str, wait: float) -> int:
    """Draw one page to a file — the way to see a page without the hardware."""
    pages = build_pages((320, 240), cfg)
    face = pages[HOME]
    face.start()
    deadline = time.monotonic() + wait
    while (page_name in ("clock", "sky") and face.weather is not None
           and face.weather.latest() is None):
        if time.monotonic() >= deadline:
            log.warning("no weather after %.0fs: %s", wait,
                        face.weather.error or "still trying")
            break
        time.sleep(0.2)
    image = pages[PAGE_NAMES.index(page_name)].frame(now=datetime.now())
    face.stop()
    if image is None:
        return 1
    image.save(path)
    print(f"wrote {path}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="ccdisp.clock", description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--png", metavar="FILE",
                        help="render a single frame to FILE and exit")
    parser.add_argument("--page", choices=PAGE_NAMES, default="clock",
                        help="which page --png draws (default: clock)")
    parser.add_argument("--wait", type=float, default=15.0,
                        help="seconds --png waits for the first weather reading")
    args = parser.parse_args(argv)

    level = logging.DEBUG if os.environ.get("CCDISP_DEBUG") else logging.INFO
    logging.basicConfig(level=level, format="%(asctime)s %(levelname)s %(message)s")

    cfg = Config.load()
    if args.png:
        return _render_png(cfg, args.png, args.page, args.wait)

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
