"""The far-right page: today's sky.

The sun's whole path for the day, drawn against a sky whose colour follows the
real solar elevation — dark blue at night, a warm band at dawn and dusk, blue
in daylight — with the sun where it actually is right now.  Underneath: day
length and how it changed since yesterday, and the moon's phase.

All of it is computed here from the coordinates the weather already uses (a
low-precision solar ephemeris and a mean synodic month), so the page works
offline and never adds a request.  Accuracy is a minute or so for sunrise and
a few hours for the moon's phases, which is far below what the panel can show.
"""

from __future__ import annotations

import math
import random
import time
from datetime import date, datetime, timedelta, timezone
from typing import Callable

from PIL import Image, ImageDraw, ImageFont

from .config import Location
from .theme import (BG, DIM, FG, MONTHS_SHORT, RULE, SANS, SANS_BOLD, SUPERSAMPLE,
                    smooth)

SUN = (255, 206, 84)
SUN_DIM = (120, 104, 70)
MOON_LIT = (232, 234, 242)
MOON_DARK = (46, 48, 62)
GROUND = (24, 23, 31)

HORIZON = 106      # y of the horizon line
SCENE_BOTTOM = 146  # the scene ends here; the facts start below
CURVE_X0, CURVE_X1 = 10, 310
REFRACTION = -0.833  # sunrise/sunset: upper limb on the horizon, refracted

SYNODIC = 29.530588853
NEW_MOON_EPOCH = datetime(2000, 1, 6, 18, 14, tzinfo=timezone.utc)


# -- astronomy -----------------------------------------------------------
def sun_elevation(when: datetime, lat: float, lon: float) -> float:
    """Solar elevation in degrees (NOAA low-precision formulae)."""
    n = when.timestamp() / 86400.0 + 2440587.5 - 2451545.0
    mean_long = (280.460 + 0.9856474 * n) % 360
    anomaly = math.radians((357.528 + 0.9856003 * n) % 360)
    ecl_long = math.radians(mean_long + 1.915 * math.sin(anomaly)
                            + 0.020 * math.sin(2 * anomaly))
    obliquity = math.radians(23.439 - 0.0000004 * n)
    decl = math.asin(math.sin(obliquity) * math.sin(ecl_long))
    ra = math.atan2(math.cos(obliquity) * math.sin(ecl_long), math.cos(ecl_long))
    gmst = (18.697374558 + 24.06570982441908 * n) % 24
    hour_angle = math.radians((gmst + lon / 15.0) * 15.0) - ra
    phi = math.radians(lat)
    s = (math.sin(phi) * math.sin(decl)
         + math.cos(phi) * math.cos(decl) * math.cos(hour_angle))
    return math.degrees(math.asin(max(-1.0, min(1.0, s))))


class SunDay:
    """One local day of solar elevation, sampled every minute."""

    def __init__(self, day: date, lat: float, lon: float) -> None:
        start = datetime(day.year, day.month, day.day).astimezone()
        self.start = start
        self.elev = [sun_elevation(start + timedelta(minutes=m), lat, lon)
                     for m in range(0, 24 * 60 + 1)]
        self.rise: datetime | None = None
        self.set: datetime | None = None
        for m in range(1, len(self.elev)):
            a, b = self.elev[m - 1] - REFRACTION, self.elev[m] - REFRACTION
            if (a < 0) == (b < 0):
                continue
            when = start + timedelta(minutes=m - 1 + a / (a - b))
            if a < 0 and self.rise is None:
                self.rise = when
            elif a >= 0:
                self.set = when

    @property
    def length(self) -> timedelta:
        """Daylight between rise and set; whole or nothing at the poles."""
        if self.rise and self.set and self.set > self.rise:
            return self.set - self.rise
        if self.rise is None and self.set is None:
            return timedelta(days=1) if self.elev[720] > REFRACTION else timedelta(0)
        # rises without setting (or the reverse): count to the day's edge
        end = self.set or self.start + timedelta(days=1)
        return end - (self.rise or self.start)


def moon_age(when: datetime) -> float:
    """Days since the last new moon."""
    return ((when - NEW_MOON_EPOCH).total_seconds() / 86400.0) % SYNODIC


def moon_phase_name(age: float) -> str:
    for limit, name in ((1.0, "новолуние"), (6.4, "растущий серп"),
                        (8.4, "первая четверть"), (13.8, "растущая луна"),
                        (15.8, "полнолуние"), (21.1, "убывающая луна"),
                        (23.1, "последняя четверть"), (28.5, "убывающий серп")):
        if age < limit:
            return name
    return "новолуние"


def moon_illumination(age: float) -> float:
    return (1 - math.cos(2 * math.pi * age / SYNODIC)) / 2


# -- drawing helpers ------------------------------------------------------
def _mix(a, b, t: float):
    t = max(0.0, min(1.0, t))
    return tuple(int(x + (y - x) * t) for x, y in zip(a, b))


# (elevation, zenith colour, horizon colour), interpolated in between
SKY_STOPS = (
    (-18, (5, 7, 18), (10, 12, 30)),
    (-8, (12, 16, 44), (44, 40, 86)),
    (-2, (28, 36, 84), (196, 112, 86)),
    (3, (52, 92, 160), (236, 160, 104)),
    (12, (46, 104, 186), (132, 176, 220)),
    (40, (34, 96, 190), (120, 170, 226)),
)


def sky_colours(elevation: float):
    stops = SKY_STOPS
    if elevation <= stops[0][0]:
        return stops[0][1], stops[0][2]
    for (e0, z0, h0), (e1, z1, h1) in zip(stops, stops[1:]):
        if elevation <= e1:
            t = (elevation - e0) / (e1 - e0)
            return _mix(z0, z1, t), _mix(h0, h1, t)
    return stops[-1][1], stops[-1][2]


def _hm(when: datetime | None) -> str:
    return f"{when:%H:%M}" if when else "--:--"


def _duration(delta: timedelta) -> str:
    minutes = int(round(delta.total_seconds() / 60))
    return f"{minutes // 60} ч {minutes % 60:02d} мин"


def draw_moon(size: int, age: float, southern: bool = False) -> Image.Image:
    """The moon's disc with its lit part, as an RGBA tile of size x size."""
    s = SUPERSAMPLE
    big = size * s
    r = big / 2 - s
    c = big / 2
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse([c - r, c - r, c + r, c + r], fill=MOON_DARK)
    p = 2 * math.pi * age / SYNODIC
    for row in range(int(c - r), int(c + r) + 1):
        y = row + 0.5 - c
        if abs(y) > r:
            continue
        w = math.sqrt(r * r - y * y)
        if p < math.pi:            # waxing: lit from the right limb
            x0, x1 = w * math.cos(p), w
        else:                      # waning: lit from the left limb
            x0, x1 = -w, -w * math.cos(p)
        if x1 > x0:
            d.line([c + x0, row, c + x1, row], fill=MOON_LIT)
    if southern:
        img = img.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
    return smooth(img)


class Sky:
    def __init__(self, size: tuple[int, int],
                 locate: Callable[[], Location | None]) -> None:
        self.size = size
        self.locate = locate
        self._days: dict[tuple, SunDay] = {}
        self._shown: tuple | None = None
        self.f_small = ImageFont.truetype(SANS, 10)
        self.f_label = ImageFont.truetype(SANS, 11)
        self.f_big = ImageFont.truetype(SANS_BOLD, 19)
        self.f_mid = ImageFont.truetype(SANS_BOLD, 12)
        # the same stars every night: they are scenery, not weather
        rng = random.Random(1969)
        self.stars = [(rng.uniform(4, size[0] - 4), rng.uniform(26, HORIZON - 8),
                       rng.choice((1, 1, 1, 2))) for _ in range(46)]

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
        return max(0.5, 60.0 - now % 60.0)

    def frame(self, now: datetime | None = None) -> Image.Image | None:
        now = now or datetime.now()
        loc = self.locate()
        key = (now.date(), now.hour, now.minute,
               (round(loc.latitude, 3), round(loc.longitude, 3)) if loc else None)
        if key == self._shown:
            return None
        self._shown = key
        return self.render(now, loc)

    # -- drawing ----------------------------------------------------------
    def _sun_day(self, day: date, loc: Location) -> SunDay:
        key = (day, round(loc.latitude, 3), round(loc.longitude, 3))
        if key not in self._days:
            if len(self._days) > 4:
                self._days.clear()
            self._days[key] = SunDay(day, loc.latitude, loc.longitude)
        return self._days[key]

    def render(self, now: datetime, loc: Location | None) -> Image.Image:
        width, height = self.size
        img = Image.new("RGB", self.size, BG)
        d = ImageDraw.Draw(img)
        if loc is None:
            d.text((width / 2, height / 2), "жду координат…", font=self.f_label,
                   fill=DIM, anchor="mm")
            return img

        today = self._sun_day(now.date(), loc)
        yesterday = self._sun_day(now.date() - timedelta(days=1), loc)
        minute = now.hour * 60 + now.minute
        elev_now = today.elev[minute] + (today.elev[minute + 1]
                                         - today.elev[minute]) * now.second / 60
        self._scene(img, now, today, elev_now)
        self._facts(img, now, today, yesterday, loc)
        return img

    def _scene(self, img: Image.Image, now: datetime, day: SunDay,
               elev_now: float) -> None:
        width, _ = self.size
        d = ImageDraw.Draw(img)

        zenith, horizon = sky_colours(elev_now)
        for y in range(HORIZON):
            t = (y / HORIZON) ** 1.6
            d.line([0, y, width, y], fill=_mix(zenith, horizon, t))
        if elev_now < -6:
            fade = min(1.0, (-6 - elev_now) / 8)
            for x, y, r in self.stars:
                c = _mix(zenith, (220, 224, 240), fade * (0.55 if r == 1 else 0.9))
                d.rectangle([x, y, x + r - 1, y + r - 1], fill=c)
        d.rectangle([0, HORIZON, width, SCENE_BOTTOM], fill=GROUND)

        top, bottom = max(day.elev), min(day.elev)
        up = (HORIZON - 30) / max(10.0, top)
        down = (SCENE_BOTTOM - HORIZON - 12) / max(10.0, -bottom)

        def point(minutes: float, elev: float) -> tuple[float, float]:
            x = CURVE_X0 + (CURVE_X1 - CURVE_X0) * minutes / 1440
            y = HORIZON - elev * (up if elev > 0 else down)
            return x * SUPERSAMPLE, y * SUPERSAMPLE

        s = SUPERSAMPLE
        layer = Image.new("RGBA", (width * s, SCENE_BOTTOM * s), (0, 0, 0, 0))
        ld = ImageDraw.Draw(layer)
        for m in range(0, 1440, 8):
            elev = day.elev[m]
            x, y = point(m, elev)
            above = elev > REFRACTION
            r = (1.6 if above else 1.0) * s
            colour = (255, 226, 150, 230) if above else (130, 130, 160, 150)
            ld.ellipse([x - r, y - r, x + r, y + r], fill=colour)

        minute = now.hour * 60 + now.minute + now.second / 60
        sx, sy = point(minute, elev_now)
        if elev_now > REFRACTION:
            for radius, alpha in ((22, 26), (15, 46), (10, 80)):
                rr = radius * s
                ld.ellipse([sx - rr, sy - rr, sx + rr, sy + rr], fill=SUN + (alpha,))
            rr = 7 * s
            ld.ellipse([sx - rr, sy - rr, sx + rr, sy + rr], fill=SUN + (255,))
        else:
            rr = 5 * s
            ld.ellipse([sx - rr, sy - rr, sx + rr, sy + rr],
                       outline=SUN_DIM + (255,), width=s * 2)
        scene = smooth(layer)
        img.paste(scene, (0, 0), scene)

        d.line([0, HORIZON, width, HORIZON], fill=(92, 88, 110))
        for when, arrow in ((day.rise, "↑"), (day.set, "↓")):
            if when is None:
                continue
            m = (when - day.start).total_seconds() / 60
            x = CURVE_X0 + (CURVE_X1 - CURVE_X0) * m / 1440
            label = f"{arrow}{when:%H:%M}"
            lw = d.textlength(label, font=self.f_small)
            x = max(4, min(width - lw - 4, x - lw / 2))
            d.text((x, HORIZON + 4), label, font=self.f_small, fill=(196, 190, 210))

        degrees = round(elev_now)
        sign = "+" if degrees > 0 else "−" if degrees < 0 else ""
        d.text((8, 6), f"солнце {sign}{abs(degrees)}°", font=self.f_small,
               fill=(214, 218, 236))
        d.text((width - 8, 6), f"{now:%H:%M}", font=self.f_small,
               fill=(214, 218, 236), anchor="ra")

    def _facts(self, img: Image.Image, now: datetime, today: SunDay,
               yesterday: SunDay, loc: Location) -> None:
        width, height = self.size
        d = ImageDraw.Draw(img)
        top = SCENE_BOTTOM + 8

        d.text((12, top), "световой день", font=self.f_small, fill=DIM)
        length = today.length
        if length >= timedelta(days=1):
            headline = "полярный день"
        elif length <= timedelta(0):
            headline = "полярная ночь"
        else:
            headline = _duration(length)
        d.text((12, top + 14), headline, font=self.f_big, fill=FG)

        diff = round((length - yesterday.length).total_seconds() / 60)
        if diff:
            sign = "+" if diff > 0 else "−"
            change = f"{sign}{abs(diff)} мин к вчерашнему"
        else:
            change = "столько же, сколько вчера"
        d.text((12, top + 40), change, font=self.f_label,
               fill=(150, 200, 140) if diff > 0 else (210, 150, 130) if diff < 0 else DIM)
        d.text((12, top + 57), f"восход {_hm(today.rise)} · закат {_hm(today.set)}",
               font=self.f_small, fill=DIM)

        d.line([196, top + 2, 196, height - 10], fill=RULE)

        age = moon_age(now.astimezone())
        moon = draw_moon(36, age, southern=loc.latitude < 0)
        img.paste(moon, (204, top + 6), moon)
        tx = 246
        d.text((tx, top + 2), "луна", font=self.f_small, fill=DIM)
        # one word per line when the name does not fit: "последняя / четверть"
        name = moon_phase_name(age)
        font = self.f_mid
        lines = [name] if d.textlength(name, font=font) <= width - tx - 4 else name.split(" ")
        if any(d.textlength(part, font=font) > width - tx - 4 for part in lines):
            font = self.f_label
        for i, part in enumerate(lines):
            d.text((tx, top + 15 + i * 14), part, font=font, fill=FG)
        d.text((tx, top + 17 + len(lines) * 14), f"{moon_illumination(age) * 100:.0f}%",
               font=self.f_label, fill=DIM)

        # whichever of full or new moon comes first
        to_full = (SYNODIC / 2 - age) % SYNODIC
        to_new = (SYNODIC - age) % SYNODIC
        days, label = (to_full, "полнолуние") if to_full < to_new else (to_new, "новолуние")
        when = (now + timedelta(days=days)).date()
        d.text((206, top + 57), f"{label} {when.day} {MONTHS_SHORT[when.month - 1]}",
               font=self.f_small, fill=DIM)
