"""Colours, fonts and small text helpers shared by every screen.

Everything is laid out in absolute pixels for a 320x240 panel: there is no room
for anything adaptive, and nothing else will ever drive this hardware.
"""

from __future__ import annotations

from PIL import Image

SANS = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
SANS_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

BG = (18, 18, 24)
HEADER_BG = (30, 30, 42)
FG = (232, 232, 238)
DIM = (140, 140, 160)
RULE = (60, 60, 78)
ACCENT = (255, 92, 72)     # the one warm colour: second hand, today, weekends
FAINT = (78, 78, 96)

HEADER_H = 22

WEEKDAYS = ("понедельник", "вторник", "среда", "четверг", "пятница",
            "суббота", "воскресенье")
WEEKDAYS_SHORT = ("пн", "вт", "ср", "чт", "пт", "сб", "вс")
MONTHS = ("января", "февраля", "марта", "апреля", "мая", "июня", "июля",
          "августа", "сентября", "октября", "ноября", "декабря")
MONTHS_NOMINATIVE = ("январь", "февраль", "март", "апрель", "май", "июнь", "июль",
                     "август", "сентябрь", "октябрь", "ноябрь", "декабрь")
MONTHS_SHORT = ("янв", "фев", "мар", "апр", "мая", "июн", "июл", "авг", "сен",
                "окт", "ноя", "дек")

# Pillow draws without antialiasing.  Anything round or slanted is drawn this
# many times larger and shrunk with Image.reduce(), which is a plain box filter
# and costs about a millisecond at panel size.
SUPERSAMPLE = 3


def shorten_middle(text: str, limit: int) -> str:
    """Collapse the middle of an over-long single line, keeping both ends."""
    if len(text) <= limit:
        return text
    keep = limit - 1
    head = (keep + 1) // 2
    return text[:head] + "…" + text[len(text) - (keep - head):]


def plural(n: int, one: str, few: str, many: str) -> str:
    """Russian noun form for n: 1 день, 2 дня, 5 дней."""
    n = abs(n)
    if n % 10 == 1 and n % 100 != 11:
        return one
    if 2 <= n % 10 <= 4 and not 12 <= n % 100 <= 14:
        return few
    return many


def smooth(image: Image.Image) -> Image.Image:
    """Shrink a SUPERSAMPLE-sized drawing back to panel pixels."""
    return image.reduce(SUPERSAMPLE)
