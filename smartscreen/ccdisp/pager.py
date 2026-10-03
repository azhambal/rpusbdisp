"""Horizontal pages you swipe between, like StandBy on a phone lying on its side.

A page is anything with start/stop/frame/invalidate/timeout/tap (see Page).
The pager owns the gestures and the transitions; pages only ever draw
themselves full-screen at offset zero.

The panel is the limit on smoothness, not Python: it hangs off USB full speed,
and fb_defio flushes at most 16 times a second.  So the content follows the
finger at whatever rate the touchscreen reports, and the settle animation is
short and time-based — on a slow flush it simply shows fewer frames of the same
movement instead of running long.
"""

from __future__ import annotations

import logging
import time
from dataclasses import dataclass
from datetime import datetime
from typing import Protocol

from PIL import Image, ImageDraw

from .theme import FG, FAINT

log = logging.getLogger("ccdisp.pager")

TAP_SLOP = 24          # touch reports quantise to ~5px; allow a finger to wobble
DRAG_START = 12        # px of horizontal travel before the page starts to follow
COMMIT_FRACTION = 0.3  # released past this share of the width: turn the page
FLICK_PX = 30          # ...or a short fast flick does it
FLICK_SECONDS = 0.3
SETTLE_SECONDS = 0.28
FRAME_SECONDS = 1 / 30
DOTS_LINGER = 1.6      # page dots stay this long after the page settles
EDGE_RESISTANCE = 0.3  # dragging past the first/last page moves this much


class Page(Protocol):
    def start(self) -> None: ...
    def stop(self) -> None: ...
    def frame(self, now: datetime | None = None) -> Image.Image | None: ...
    def invalidate(self) -> None: ...
    def timeout(self, now: float | None = None) -> float: ...
    def tap(self, x: int, y: int) -> None: ...


@dataclass
class _Settle:
    start: float
    origin: float   # offset when the finger lifted
    target: float   # offset to end on: 0 (stay) or +-width (turn)
    neighbour: int  # page index on the far side of the movement

    def offset(self, now: float) -> float:
        t = min(1.0, (now - self.start) / SETTLE_SECONDS)
        ease = 1 - (1 - t) ** 3
        return self.origin + (self.target - self.origin) * ease

    def done(self, now: float) -> bool:
        return now - self.start >= SETTLE_SECONDS


class Pager:
    """Index 0 is the leftmost page.  Offset > 0 means the content moves right,
    i.e. the page on the left is sliding in."""

    def __init__(self, size: tuple[int, int], pages: list[Page], home: int) -> None:
        self.size = size
        self.pages = pages
        self.home = home
        self.index = home
        self._base: Image.Image | None = None    # last full frame of the current page
        self._cache: dict[int, Image.Image] = {}  # neighbours rendered for a drag
        self._down: tuple[int, int, float] | None = None
        self._dragging = False
        self._offset = 0.0
        self._settle: _Settle | None = None
        self._dots_until = 0.0
        self._had_dots = False
        self._dirty = True

    # -- lifecycle --------------------------------------------------------
    def start(self) -> None:
        for page in self.pages:
            page.start()

    def stop(self) -> None:
        for page in self.pages:
            page.stop()

    @property
    def current(self) -> Page:
        return self.pages[self.index]

    @property
    def busy(self) -> bool:
        return self._dragging or self._settle is not None

    # -- input ------------------------------------------------------------
    def touch(self, kind: str, x: int, y: int) -> None:
        width = self.size[0]
        now = time.monotonic()
        if kind == "down":
            if self._settle is not None:
                # caught mid-flight: finish the turn now rather than fight it
                self._finish_settle()
            self._down = (x, y, now)
            self._dragging = False
            return
        if self._down is None:
            return
        x0, y0, t0 = self._down
        dx, dy = x - x0, y - y0

        if kind == "move":
            if not self._dragging:
                if abs(dx) < DRAG_START or abs(dx) < abs(dy):
                    return
                self._dragging = True
                self._cache.clear()
            self._offset = self._clamp(dx)
            self._dots_until = float("inf")
            self._dirty = True
            return

        # up
        self._down = None
        if not self._dragging and abs(dx) <= TAP_SLOP and abs(dy) <= TAP_SLOP:
            self.current.tap(x, y)
            return
        quick = now - t0 <= FLICK_SECONDS and abs(dx) >= FLICK_PX and abs(dx) > abs(dy)
        far = abs(dx) >= width * COMMIT_FRACTION and self._dragging
        step = -1 if dx > 0 else 1   # finger moves right -> the left page comes in
        target_index = self.index + step
        offset = self._clamp(dx) if self._dragging else 0.0
        self._dragging = False
        if (quick or far) and 0 <= target_index < len(self.pages):
            self._begin_settle(offset, -step * width, target_index)
        elif offset:
            self._begin_settle(offset, 0.0, self.index + (-1 if offset > 0 else 1))
        self._dots_until = time.monotonic() + DOTS_LINGER
        self._dirty = True

    def go(self, index: int) -> None:
        """Jump straight to a page, no animation."""
        self.index = max(0, min(len(self.pages) - 1, index))
        self.current.invalidate()
        self._base = None

    def _clamp(self, dx: float) -> float:
        width = self.size[0]
        if (dx > 0 and self.index == 0) or (dx < 0 and self.index == len(self.pages) - 1):
            return dx * EDGE_RESISTANCE
        return max(-width, min(width, dx))

    def _begin_settle(self, origin: float, target: float, neighbour: int) -> None:
        self._settle = _Settle(time.monotonic(), origin, target, neighbour)

    def _finish_settle(self) -> None:
        settle = self._settle
        self._settle = None
        self._offset = 0.0
        if settle is not None and settle.target != 0:
            log.debug("page %d -> %d", self.index, settle.neighbour)
            self.index = settle.neighbour
            self._base = None
            self.current.invalidate()
        self._cache.clear()
        self._dirty = True

    # -- output -----------------------------------------------------------
    def timeout(self) -> float:
        """How long the main loop may sleep before frame() has work to do."""
        if self.busy:
            return FRAME_SECONDS
        wait = self.current.timeout()
        now = time.monotonic()
        if self._dots_until > now:
            wait = min(wait, self._dots_until - now)
        return max(0.01, wait)

    def frame(self) -> Image.Image | None:
        """What the panel should show now, or None when it is already right."""
        mono = time.monotonic()
        now = datetime.now()

        if self._settle is not None:
            offset = self._settle.offset(mono)
            neighbour = self._settle.neighbour
            if self._settle.done(mono):
                self._finish_settle()
                self._dots_until = mono + DOTS_LINGER
            else:
                return self.compose(offset, neighbour, now, dots=True)
        elif self._dragging:
            if not self._dirty:
                return None
            self._dirty = False
            neighbour = self.index + (-1 if self._offset > 0 else 1)
            return self.compose(self._offset, neighbour, now, dots=True)

        fresh = self.current.frame(now)
        if fresh is not None:
            self._base = fresh
        elif self._base is None:
            self.current.invalidate()
            self._base = self.current.frame(now)
        dots = self._dots_until > mono
        if fresh is None and not self._dirty and dots == self._had_dots:
            return None
        self._dirty = False
        self._had_dots = dots
        if not dots:
            return self._base
        img = self._base.copy()
        self._draw_dots(img, float(self.index))
        return img

    def _page_image(self, index: int, now: datetime) -> Image.Image:
        if index == self.index and self._base is not None:
            return self._base
        cached = self._cache.get(index)
        if cached is None:
            page = self.pages[index]
            page.invalidate()
            cached = page.frame(now) or Image.new("RGB", self.size)
            # the page must redraw itself in full once it becomes current
            page.invalidate()
            self._cache[index] = cached
        return cached

    def compose(self, offset: float, neighbour: int, now: datetime,
                 dots: bool) -> Image.Image:
        """One frame mid-turn: the current page shifted by `offset`, the
        neighbour filling the gap.  tools/screenshots.py films swipes with it."""
        width, height = self.size
        img = Image.new("RGB", self.size, (0, 0, 0))
        shift = int(round(offset))
        img.paste(self._page_image(self.index, now), (shift, 0))
        if 0 <= neighbour < len(self.pages) and shift:
            other = self._page_image(neighbour, now)
            img.paste(other, (shift - width if shift > 0 else shift + width, 0))
        if dots:
            self._draw_dots(img, self.index - offset / width)
        return img

    def _draw_dots(self, img: Image.Image, position: float) -> None:
        """Page dots in a pill at the top centre.  The bright one slides with
        the drag, so it reads as 'where am I' at a glance."""
        count = len(self.pages)
        gap, r = 10, 2.5
        span = gap * (count - 1)
        cx = self.size[0] / 2
        top, bottom = 4, 17
        d = ImageDraw.Draw(img)
        d.rounded_rectangle([cx - span / 2 - 10, top, cx + span / 2 + 10, bottom],
                            radius=(bottom - top) / 2, fill=(10, 10, 14))
        cy = (top + bottom) / 2
        for i in range(count):
            x = cx - span / 2 + gap * i
            d.ellipse([x - r + 0.5, cy - r + 0.5, x + r - 0.5, cy + r - 0.5],
                      fill=FAINT)
        position = max(0.0, min(count - 1.0, position))
        x = cx - span / 2 + gap * position
        d.ellipse([x - r - 0.5, cy - r - 0.5, x + r + 0.5, cy + r + 0.5], fill=FG)
