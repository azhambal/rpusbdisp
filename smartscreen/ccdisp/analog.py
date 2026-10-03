"""The page left of centre: an analogue dial, the way StandBy shows one.

The dial itself never changes, so it is drawn once.  The hands are redrawn
every second on a transparent layer and laid over it; Framebuffer.blit_changed
then sends only the box the second hand swept, not the whole face.
"""

from __future__ import annotations

import math
import time
from datetime import datetime

from PIL import Image, ImageDraw, ImageFont

from .config import Config
from .theme import (ACCENT, BG, DIM, FAINT, FG, SANS, SANS_BOLD, SUPERSAMPLE,
                    WEEKDAYS_SHORT, smooth)

DIAL_FACE = (26, 26, 34)
HAND = FG


class AnalogClock:
    """Same contract as clockface.Face: frame() is None while nothing moved."""

    def __init__(self, size: tuple[int, int], cfg: Config) -> None:
        self.size = size
        self.seconds = bool(cfg.extra.get("analog_seconds", True))
        width, height = size
        self.radius = min(width, height) // 2 - 6
        self.centre = (width // 2, height // 2)
        self._dial: Image.Image | None = None
        self._small = ImageFont.truetype(SANS, 11)
        self._big = ImageFont.truetype(SANS_BOLD, 20)
        self._shown: tuple | None = None

    # -- pager contract ---------------------------------------------------
    def start(self) -> None:
        pass

    def stop(self) -> None:
        pass

    def tap(self, _x: int, _y: int) -> None:
        pass

    def invalidate(self) -> None:
        self._shown = None

    def timeout(self, now: float | None = None) -> float:
        now = time.time() if now is None else now
        if self.seconds:
            return max(0.05, 1.0 - now % 1.0)
        # the minute hand creeps: once every 10 s is smooth enough for it
        return max(0.5, 10.0 - now % 10.0)

    def frame(self, now: datetime | None = None) -> Image.Image | None:
        now = now or datetime.now()
        state = ((now.hour, now.minute, now.second) if self.seconds
                 else (now.hour, now.minute, now.second // 10))
        if state == self._shown:
            return None
        self._shown = state
        return self.render(now)

    # -- drawing ----------------------------------------------------------
    def render(self, now: datetime) -> Image.Image:
        img = self.dial().copy()
        img.alpha_composite(self._hands(now))
        self._date(img, now)
        return img.convert("RGB")

    def dial(self) -> Image.Image:
        if self._dial is None:
            self._dial = self._draw_dial()
        return self._dial

    def _draw_dial(self) -> Image.Image:
        s = SUPERSAMPLE
        w, h = self.size
        cx, cy, r = self.centre[0] * s, self.centre[1] * s, self.radius * s
        img = Image.new("RGBA", (w * s, h * s), BG)
        d = ImageDraw.Draw(img)
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=DIAL_FACE)

        for tick in range(60):
            a = math.radians(tick * 6)
            hour = tick % 5 == 0
            if hour and tick % 15 == 0:
                continue  # the numerals stand in for the quarter ticks
            outer = r - 5 * s
            inner = outer - (12 if hour else 5) * s
            d.line([cx + math.sin(a) * inner, cy - math.cos(a) * inner,
                    cx + math.sin(a) * outer, cy - math.cos(a) * outer],
                   fill=FG if hour else FAINT, width=(3 if hour else 1) * s)

        font = ImageFont.truetype(SANS_BOLD, 22 * s)
        for n, tick in ((12, 0), (3, 15), (6, 30), (9, 45)):
            a = math.radians(tick * 6)
            dist = r - 20 * s
            tx, ty = cx + math.sin(a) * dist, cy - math.cos(a) * dist
            d.text((tx, ty), str(n), font=font, fill=FG, anchor="mm")
        return smooth(img)

    def _hands(self, now: datetime) -> Image.Image:
        s = SUPERSAMPLE
        w, h = self.size
        cx, cy, r = self.centre[0] * s, self.centre[1] * s, self.radius * s
        layer = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)

        sec = now.second
        minute = now.minute + sec / 60
        hour = now.hour % 12 + minute / 60

        def hand(turns: float, length: float, tail: float, width: float, color):
            a = turns * 2 * math.pi
            dx, dy = math.sin(a), -math.cos(a)
            x0, y0 = cx - dx * tail, cy - dy * tail
            x1, y1 = cx + dx * length, cy + dy * length
            d.line([x0, y0, x1, y1], fill=color, width=int(width))
            # round the tip: a bare wide line ends square and looks cut off
            rr = width / 2
            d.ellipse([x1 - rr, y1 - rr, x1 + rr, y1 + rr], fill=color)

        hand(hour / 12, r * 0.52, r * 0.08, 7 * s, HAND)
        hand(minute / 60, r * 0.80, r * 0.08, 5 * s, HAND)
        if self.seconds:
            hand(sec / 60, r * 0.86, r * 0.18, 1.6 * s, ACCENT)
        pin = 5 * s
        d.ellipse([cx - pin, cy - pin, cx + pin, cy + pin],
                  fill=ACCENT if self.seconds else HAND)
        d.ellipse([cx - 2 * s, cy - 2 * s, cx + 2 * s, cy + 2 * s], fill=DIAL_FACE)
        return smooth(layer)

    def _date(self, img: Image.Image, now: datetime) -> None:
        """A watch-style date window in the corner, clear of every hand."""
        d = ImageDraw.Draw(img)
        d.text((10, 8), WEEKDAYS_SHORT[now.weekday()], font=self._small, fill=ACCENT)
        d.text((10, 21), str(now.day), font=self._big, fill=FG)
        d.text((10, 44), f"{now:%H:%M}", font=self._small, fill=DIM)
