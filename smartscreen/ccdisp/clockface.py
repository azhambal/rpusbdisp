"""The idle face: a big clock over the current weather, for a 320x240 panel.

Same absolute-pixel approach as every screen here (see theme.py) — at this size nothing adaptive is worth
the complexity.  The weather icons are drawn from primitives instead of a font
or bitmaps: DejaVu has no usable weather glyphs, and a handful of circles and
lines scale down to the 18px forecast cells without turning to mush.
"""

from __future__ import annotations

from dataclasses import dataclass
import time
from datetime import datetime

from PIL import Image, ImageDraw, ImageFont

from . import weather as wx
from .config import Config
from .theme import (BG, DIM, FG, HEADER_BG, HEADER_H, MONTHS, RULE, SANS,
                    SANS_BOLD, WEEKDAYS, WEEKDAYS_SHORT, shorten_middle)

SUN = (255, 199, 64)
MOON = (226, 231, 248)
CLOUD = (176, 184, 204)
CLOUD_DARK = (132, 139, 162)
RAIN = (96, 166, 232)
SNOWFLAKE = (214, 232, 255)
BOLT = (250, 206, 74)
HAZE = (150, 156, 176)
WARN = (220, 180, 60)

MINUS = "\u2212"  # a real minus: hyphen next to 74px digits reads as a dash

# Vertical bands.  Everything below is drawn relative to these.
CLOCK_TOP = HEADER_H
CLOCK_BOTTOM = 116
WEATHER_TOP = 120
WEATHER_BOTTOM = 190
STRIP_TOP = 194


class Fonts:
    def __init__(self) -> None:
        self.clock = ImageFont.truetype(SANS_BOLD, 74)
        self.clock_small = ImageFont.truetype(SANS_BOLD, 58)
        self.temp = ImageFont.truetype(SANS_BOLD, 36)
        self.label = ImageFont.truetype(SANS, 13)
        self.small = ImageFont.truetype(SANS, 10)
        self.small_bold = ImageFont.truetype(SANS_BOLD, 11)
        self.date = ImageFont.truetype(SANS, 12)


_fonts: Fonts | None = None


def fonts() -> Fonts:
    global _fonts
    if _fonts is None:
        _fonts = Fonts()
    return _fonts


def format_temp(value: float | None) -> str:
    """Russian keeps a bare zero: '+0°' looks like a rounding artefact."""
    if value is None:
        return "--°"
    n = int(round(value))
    if n == 0:
        return "0°"
    return f"+{n}°" if n > 0 else f"{MINUS}{-n}°"


def format_date(now: datetime, short: bool = False) -> str:
    day = (WEEKDAYS_SHORT if short else WEEKDAYS)[now.weekday()]
    return f"{day}, {now.day} {MONTHS[now.month - 1]}"


# -- icons ----------------------------------------------------------------
def _sun(d: ImageDraw.ImageDraw, cx: float, cy: float, r: float,
         color=SUN, rays: bool = True) -> None:
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=color)
    if not rays or r < 5:
        return
    inner, outer = r + max(1.5, r * 0.35), r + max(3.0, r * 0.85)
    width = max(1, int(r * 0.22))
    for i in range(8):
        # 45° apart; sin/cos by hand keeps the module import-free
        dx, dy = ((1, 0), (0.7071, 0.7071), (0, 1), (-0.7071, 0.7071),
                  (-1, 0), (-0.7071, -0.7071), (0, -1), (0.7071, -0.7071))[i]
        d.line([cx + dx * inner, cy + dy * inner,
                cx + dx * outer, cy + dy * outer], fill=color, width=width)


def _moon(d: ImageDraw.ImageDraw, cx: float, cy: float, r: float,
          bg=BG) -> None:
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=MOON)
    # bite a disc out of the upper right to leave a crescent
    off = r * 0.62
    d.ellipse([cx - r + off, cy - r - off * 0.35,
               cx + r + off, cy + r - off * 0.35], fill=bg)


def _cloud(d: ImageDraw.ImageDraw, x: float, y: float, w: float,
           color=CLOUD) -> None:
    h = w * 0.62
    d.rounded_rectangle([x, y + h * 0.42, x + w, y + h],
                        radius=h * 0.29, fill=color)
    for cx, cy, r in ((0.28, 0.52, 0.30), (0.55, 0.36, 0.36), (0.80, 0.52, 0.26)):
        px, py, pr = x + w * cx, y + h * cy, h * r
        d.ellipse([px - pr, py - pr, px + pr, py + pr], fill=color)


def _streaks(d: ImageDraw.ImageDraw, x: float, y: float, w: float, h: float,
             count: int, color, slant: float = 0.28, width: int = 2) -> None:
    for i in range(count):
        sx = x + w * (i + 0.5) / count
        d.line([sx + h * slant, y, sx - h * slant, y + h], fill=color, width=width)


def _flakes(d: ImageDraw.ImageDraw, x: float, y: float, w: float, h: float,
            count: int, color) -> None:
    r = max(1.0, h * 0.28)
    for i in range(count):
        cx = x + w * (i + 0.5) / count
        cy = y + h * (0.25 if i % 2 else 0.6)
        if r <= 1.6:
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=color)
            continue
        for dx, dy in ((1, 0), (0.5, 0.87), (-0.5, 0.87)):
            d.line([cx - dx * r, cy - dy * r, cx + dx * r, cy + dy * r],
                   fill=color, width=1)


def _bolt(d: ImageDraw.ImageDraw, x: float, y: float, w: float, h: float) -> None:
    d.polygon([(x + w * 0.55, y), (x + w * 0.18, y + h * 0.58),
               (x + w * 0.44, y + h * 0.58), (x + w * 0.30, y + h),
               (x + w * 0.78, y + h * 0.42), (x + w * 0.50, y + h * 0.42)],
              fill=BOLT)


def draw_icon(d: ImageDraw.ImageDraw, x: float, y: float, size: float,
              group: str, is_day: bool = True, bg=BG) -> None:
    """Draw a weather pictogram inside the size x size box at (x, y)."""
    def orb(cx, cy, r, rays=True):
        if is_day:
            _sun(d, cx, cy, r, rays=rays)
        else:
            _moon(d, cx, cy, r, bg=bg)

    if group == wx.CLEAR:
        orb(x + size * 0.5, y + size * 0.5, size * 0.26)
        return
    if group in (wx.MOSTLY_CLEAR, wx.PARTLY):
        big = group == wx.PARTLY
        orb(x + size * 0.36, y + size * (0.36 if big else 0.34),
            size * (0.19 if big else 0.22), rays=not big)
        _cloud(d, x + size * (0.22 if big else 0.30), y + size * 0.42,
               size * (0.74 if big else 0.64))
        return
    if group == wx.OVERCAST:
        _cloud(d, x + size * 0.06, y + size * 0.22, size * 0.88, CLOUD_DARK)
        return
    if group == wx.FOG:
        _cloud(d, x + size * 0.08, y + size * 0.10, size * 0.84, CLOUD_DARK)
        for i in range(3):
            ly = y + size * (0.62 + i * 0.14)
            inset = size * (0.10 if i % 2 else 0.20)
            d.line([x + inset, ly, x + size - inset, ly], fill=HAZE, width=2)
        return

    _cloud(d, x + size * 0.06, y + size * 0.08, size * 0.88,
           CLOUD_DARK if group in (wx.THUNDER, wx.SHOWERS) else CLOUD)
    fx, fy = x + size * 0.14, y + size * 0.62
    fw, fh = size * 0.72, size * 0.34
    if group == wx.THUNDER:
        _bolt(d, x + size * 0.28, fy - size * 0.04, size * 0.44, fh + size * 0.06)
    elif group == wx.SNOW:
        _flakes(d, fx, fy, fw, fh, 3, SNOWFLAKE)
    elif group == wx.FREEZING:
        _streaks(d, fx, fy, fw, fh * 0.7, 2, RAIN)
        _flakes(d, fx + fw * 0.45, fy, fw * 0.55, fh, 1, SNOWFLAKE)
    elif group == wx.DRIZZLE:
        _streaks(d, fx, fy + fh * 0.25, fw, fh * 0.5, 3, RAIN, slant=0.15, width=1)
    else:  # rain, showers
        _streaks(d, fx, fy, fw, fh, 3 if group == wx.RAIN else 4, RAIN)


# -- layout ---------------------------------------------------------------
@dataclass(frozen=True)
class FaceState:
    """Everything that can change what is drawn, so unchanged seconds cost nothing."""
    minute: str
    colon: bool
    date: str
    place: str
    note: str
    weather_stamp: int
    stale: bool


def _draw_header(d: ImageDraw.ImageDraw, width: int, now: datetime,
                 place: str, show_date: bool) -> None:
    f = fonts()
    d.rectangle([0, 0, width, HEADER_H - 1], fill=HEADER_BG)

    place_text = shorten_middle(place, 18) if place else ""
    place_w = d.textlength(place_text, font=f.small) if place_text else 0

    if show_date:
        # the full weekday is nicer, but a long place name wins the header:
        # drop to "вс" rather than overprinting.  The middle stays clear for
        # the page dots the pager draws there while you swipe.
        date_text = format_date(now)
        room = (width - 52) / 2 - 10
        if d.textlength(date_text, font=f.small) > room:
            date_text = format_date(now, short=True)
        d.text((6, 5), date_text, font=f.small, fill=DIM)
    if place_text:
        d.text((width - place_w - 6, 5), place_text, font=f.small, fill=DIM)


def _draw_clock(d: ImageDraw.ImageDraw, width: int, now: datetime,
                colon: bool, top: int, bottom: int, big: bool) -> None:
    """Hours and minutes are placed separately so a blinking colon cannot
    nudge the digits sideways: ':' and ' ' are not the same width."""
    f = fonts()
    font = f.clock if big else f.clock_small
    hh, mm = f"{now.hour:02d}", f"{now.minute:02d}"
    hw = d.textlength(hh, font=font)
    cw = d.textlength(":", font=font)
    mw = d.textlength(mm, font=font)
    x = (width - (hw + cw + mw)) / 2
    box = d.textbbox((0, 0), "08", font=font)
    y = (top + bottom - (box[3] - box[1])) / 2 - box[1]
    d.text((x, y), hh, font=font, fill=FG)
    if colon:
        d.text((x + hw, y), ":", font=font, fill=FG)
    d.text((x + hw + cw, y), mm, font=font, fill=FG)


def _draw_weather(d: ImageDraw.ImageDraw, width: int, w: wx.Weather,
                  stale: bool) -> None:
    f = fonts()
    top = WEATHER_TOP
    d.line([8, top - 4, width - 9, top - 4], fill=RULE)

    icon = 58
    draw_icon(d, 10, top, icon, w.group, w.is_day)

    x = 10 + icon + 10
    temp = format_temp(w.temp)
    d.text((x, top - 4), temp, font=f.temp, fill=DIM if stale else FG)
    tw = d.textlength(temp, font=f.temp)

    if w.high is not None and w.low is not None:
        d.text((x + tw + 8, top + 4), f"↑{format_temp(w.high)}",
               font=f.small, fill=DIM)
        d.text((x + tw + 8, top + 17), f"↓{format_temp(w.low)}",
               font=f.small, fill=DIM)

    label = w.label
    while label and d.textlength(label, font=f.label) > width - x - 8:
        label = label[:-1]
    d.text((x, top + 36), label, font=f.label, fill=FG if not stale else DIM)

    if stale:
        d.text((x, top + 52), f"данные от {w.observed:%H:%M}",
               font=f.small, fill=WARN)
    else:
        d.text((x, top + 52),
               f"ощущается {format_temp(w.feels)} · ветер {w.wind:.0f} м/с"
               f" · {w.humidity}%",
               font=f.small, fill=DIM)


def _draw_strip(d: ImageDraw.ImageDraw, width: int, height: int,
                hours: tuple[wx.Hour, ...]) -> None:
    f = fonts()
    if not hours:
        return
    d.line([8, STRIP_TOP - 4, width - 9, STRIP_TOP - 4], fill=RULE)
    cell = width / len(hours)
    for i, hour in enumerate(hours):
        cx = cell * (i + 0.5)
        stamp = f"{hour.when:%H:%M}"
        sw = d.textlength(stamp, font=f.small)
        d.text((cx - sw / 2, STRIP_TOP), stamp, font=f.small, fill=DIM)
        group = wx.describe(hour.code)[0]
        day = 6 <= hour.when.hour < 21
        draw_icon(d, cx - 9, STRIP_TOP + 11, 18, group, day)
        temp = format_temp(hour.temp)
        tw = d.textlength(temp, font=f.small_bold)
        d.text((cx - tw / 2, STRIP_TOP + 31), temp, font=f.small_bold, fill=FG)


def state_of(now: datetime, w: wx.Weather | None, place: str = "",
          note: str = "", colon: bool = True, weather_stamp: int = 0) -> FaceState:
    return FaceState(minute=f"{now.hour:02d}:{now.minute:02d}", colon=colon,
                     date=format_date(now), place=place, note=note,
                     weather_stamp=weather_stamp,
                     stale=bool(w is not None and w.stale()))


def render(size: tuple[int, int], now: datetime, w: wx.Weather | None = None, *,
           place: str = "", note: str = "",
           colon: bool = True, show_date: bool = True) -> Image.Image:
    """The whole idle face.  `note` replaces the weather block when there is none."""
    width, height = size
    img = Image.new("RGB", size, BG)
    d = ImageDraw.Draw(img)

    _draw_header(d, width, now, place or (w.place if w else ""), show_date)

    if w is None:
        # nothing to show below the clock: let it take the room instead of
        # leaving a hole where the weather would have been
        bottom = height - (24 if note else 0)
        _draw_clock(d, width, now, colon, CLOCK_TOP, bottom, big=True)
        if note:
            f = fonts()
            text = shorten_middle(note, 44)
            tw = d.textlength(text, font=f.small)
            d.text(((width - tw) / 2, height - 18), text, font=f.small, fill=DIM)
        return img

    _draw_clock(d, width, now, colon, CLOCK_TOP, CLOCK_BOTTOM, big=True)
    _draw_weather(d, width, w, w.stale())
    _draw_strip(d, width, height, w.hourly)
    return img


class Face:
    """The idle face as a unit: a weather service, a redraw budget, and frames.

    It is the centre page of the pager, and follows the same contract as the
    others: frame() returns None while the last frame is still accurate, and
    timeout() says when that stops being true.
    """

    def __init__(self, size: tuple[int, int], cfg: Config,
                 service: wx.WeatherService | None = None) -> None:
        self.size = size
        self.cfg = cfg
        if service is None and cfg.weather:
            service = wx.WeatherService(cfg)
        self.weather = service
        self._shown: FaceState | None = None

    # -- lifecycle --------------------------------------------------------
    def start(self) -> None:
        if self.weather is not None:
            self.weather.start()

    def stop(self) -> None:
        if self.weather is not None:
            self.weather.stop()

    def refresh(self) -> None:
        if self.weather is not None:
            self.weather.refresh_now()

    def tap(self, _x: int, _y: int) -> None:
        """A tap on the face asks for a fresh weather reading."""
        self.refresh()

    def invalidate(self) -> None:
        """Force the next frame() to draw, e.g. after another page covered it."""
        self._shown = None

    # -- timing -----------------------------------------------------------
    def timeout(self, now: float | None = None) -> float:
        """How long the current frame stays correct.

        With a blinking colon that is the next whole second; without one there
        is nothing to redraw until the minute rolls over.
        """
        now = time.time() if now is None else now
        if self.cfg.blink_colon:
            return max(0.05, 1.0 - now % 1.0)
        return max(0.5, min(30.0, 60.0 - now % 60.0))

    # -- drawing ----------------------------------------------------------
    def frame(self, now: datetime | None = None) -> Image.Image | None:
        """The face to show, or None when the last one is still accurate."""
        now = now or datetime.now()
        weather = place = None
        note = ""
        stamp = 0
        if self.weather is not None:
            weather = self.weather.latest()
            location = self.weather.location
            place = location.place if location else ""
            stamp = self.weather.stamp()
            if weather is None:
                error = self.weather.error
                note = f"погода: {error}" if error else "погода…"
        colon = now.second % 2 == 0 if self.cfg.blink_colon else True

        state = state_of(now, weather, place=place or "", note=note,
                         colon=colon, weather_stamp=stamp)
        if state == self._shown:
            return None
        self._shown = state
        return render(self.size, now, weather, place=place or "",
                      note=note, colon=colon, show_date=self.cfg.show_date)
