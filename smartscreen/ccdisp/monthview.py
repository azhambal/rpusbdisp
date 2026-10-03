"""The page right of centre: today on the left, the month as a grid on the right.

(Not calendar.py: that would shadow the stdlib module this file leans on.)

Weekends and the fixed Russian public holidays are tinted.  The yearly moved
days-off are not: they are decreed every autumn and no offline rule gets them
right, so the grid shows what is certain and leaves the rest alone.
"""

from __future__ import annotations

import calendar
import time
from datetime import date, datetime, timedelta

from PIL import Image, ImageDraw, ImageFont

from .config import Config
from .theme import (ACCENT, BG, DIM, FAINT, FG, MONTHS_NOMINATIVE, RULE, SANS,
                    SANS_BOLD, WEEKDAYS, WEEKDAYS_SHORT, plural)

WEEKEND = (214, 110, 98)

# (month, day) -> name; one name per run of days, on its first day
HOLIDAYS = {
    (1, 1): "Новый год", (1, 2): "", (1, 3): "", (1, 4): "", (1, 5): "", (1, 6): "",
    (1, 7): "Рождество", (1, 8): "",
    (2, 23): "23 Февраля",
    (3, 8): "8 Марта",
    (5, 1): "1 Мая",
    (5, 9): "День Победы",
    (6, 12): "День России",
    (11, 4): "День единства",
}

GRID_X = 118
GRID_W = 196
COL = GRID_W / 7
HEAD_Y = 10
ROWS_Y = 32
ROW_H = 34


def next_holiday(today: date) -> tuple[str, int]:
    """The nearest named holiday on or after today, and how many days away."""
    for ahead in range(0, 367):
        day = today + timedelta(days=ahead)
        name = HOLIDAYS.get((day.month, day.day))
        if name:
            return name, ahead
    return "", 0


class MonthView:
    def __init__(self, size: tuple[int, int], cfg: Config) -> None:
        self.size = size
        self._shown: date | None = None
        self.f_weekday = ImageFont.truetype(SANS_BOLD, 14)
        self.f_day = ImageFont.truetype(SANS_BOLD, 64)
        self.f_month = ImageFont.truetype(SANS, 15)
        self.f_head = ImageFont.truetype(SANS_BOLD, 11)
        self.f_cell = ImageFont.truetype(SANS, 14)
        self.f_cell_bold = ImageFont.truetype(SANS_BOLD, 14)
        self.f_small = ImageFont.truetype(SANS, 11)

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
        # nothing moves until midnight; wake now and then so a clock change
        # or a suspended laptop cannot leave yesterday on the screen for long
        return max(1.0, min(60.0, 60.0 - now % 60.0))

    def frame(self, now: datetime | None = None) -> Image.Image | None:
        today = (now or datetime.now()).date()
        if today == self._shown:
            return None
        self._shown = today
        return self.render(today)

    # -- drawing ----------------------------------------------------------
    def render(self, today: date) -> Image.Image:
        img = Image.new("RGB", self.size, BG)
        d = ImageDraw.Draw(img)
        self._today(d, today)
        d.line([GRID_X - 8, 12, GRID_X - 8, self.size[1] - 12], fill=RULE)
        self._grid(d, today)
        return img

    def _today(self, d: ImageDraw.ImageDraw, today: date) -> None:
        cx = (GRID_X - 8) / 2
        d.text((cx, 22), WEEKDAYS[today.weekday()].upper(), font=self.f_weekday,
               fill=ACCENT, anchor="mm")
        d.text((cx, 72), str(today.day), font=self.f_day, fill=FG, anchor="mm")
        d.text((cx, 116), MONTHS_NOMINATIVE[today.month - 1], font=self.f_month,
               fill=FG, anchor="mm")
        d.text((cx, 135), str(today.year), font=self.f_small, fill=DIM, anchor="mm")

        name, ahead = next_holiday(today)
        if not name:
            return
        d.line([cx - 30, 160, cx + 30, 160], fill=RULE)
        d.text((cx, 180), name, font=self.f_small, fill=WEEKEND, anchor="mm")
        if ahead == 0:
            when = "сегодня"
        elif ahead == 1:
            when = "завтра"
        else:
            when = f"через {ahead} {plural(ahead, 'день', 'дня', 'дней')}"
        d.text((cx, 197), when, font=self.f_small, fill=DIM, anchor="mm")

    def _grid(self, d: ImageDraw.ImageDraw, today: date) -> None:
        for i, name in enumerate(WEEKDAYS_SHORT):
            d.text((GRID_X + COL * (i + 0.5), HEAD_Y + 6), name.upper(),
                   font=self.f_head, fill=WEEKEND if i >= 5 else DIM, anchor="mm")

        weeks = calendar.Calendar(firstweekday=0).monthdayscalendar(today.year, today.month)
        # five-week months get the spare row spread out rather than left empty
        row_h = ROW_H if len(weeks) == 6 else ROW_H * 6 / 5
        for row, week in enumerate(weeks):
            cy = ROWS_Y + row_h * (row + 0.5)
            for col, day in enumerate(week):
                if not day:
                    continue
                cx = GRID_X + COL * (col + 0.5)
                holiday = (today.month, day) in HOLIDAYS
                if day == today.day:
                    rr = 13
                    d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=ACCENT)
                    d.text((cx, cy), str(day), font=self.f_cell_bold, fill=FG,
                           anchor="mm")
                    continue
                past = day < today.day
                if col >= 5 or holiday:
                    color = WEEKEND
                else:
                    color = FG
                if past:
                    color = tuple(int(c * 0.55 + b * 0.45) for c, b in zip(color, BG))
                d.text((cx, cy), str(day), font=self.f_cell, fill=color, anchor="mm")
                if holiday:
                    d.line([cx - 4, cy + 10, cx + 4, cy + 10], fill=FAINT, width=2)
