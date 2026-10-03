"""Open-Meteo client with a background refresh thread.

Open-Meteo needs no API key and no account, which is the whole reason it is
here: the panel should work on a fresh machine without anyone registering
anywhere.  Fetching happens on its own thread because the clock's selector
loop owns the screen and must never sit in a socket read waiting on a weather
server.  Readers get whatever was last fetched, plus its age, and decide for
themselves whether that is still worth drawing.
"""

from __future__ import annotations

import json
import logging
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from datetime import datetime

from .config import Config, Location, resolve_location

log = logging.getLogger("ccdisp.weather")

ENDPOINT = "https://api.open-meteo.com/v1/forecast"
USER_AGENT = "ccdisp-clock/1.0 (+https://github.com/azhambal/rpusbdisp)"

CURRENT = ("temperature_2m", "apparent_temperature", "relative_humidity_2m",
           "wind_speed_10m", "weather_code", "is_day")
HOURLY = ("temperature_2m", "weather_code")
DAILY = ("temperature_2m_max", "temperature_2m_min", "sunrise", "sunset")

# How old a reading may get before the face admits it is out of date.
STALE_AFTER = 3 * 3600

# WMO 4677 as Open-Meteo uses it.  Grouped into what an icon can actually show.
CLEAR, MOSTLY_CLEAR, PARTLY, OVERCAST = "clear", "mostly_clear", "partly", "overcast"
FOG, DRIZZLE, RAIN, FREEZING = "fog", "drizzle", "rain", "freezing"
SNOW, SHOWERS, THUNDER = "snow", "showers", "thunder"

_CODES: dict[int, tuple[str, str]] = {
    0: (CLEAR, "ясно"),
    1: (MOSTLY_CLEAR, "малооблачно"),
    2: (PARTLY, "переменная облачность"),
    3: (OVERCAST, "пасмурно"),
    45: (FOG, "туман"),
    48: (FOG, "изморозь"),
    51: (DRIZZLE, "слабая морось"),
    53: (DRIZZLE, "морось"),
    55: (DRIZZLE, "сильная морось"),
    56: (FREEZING, "ледяная морось"),
    57: (FREEZING, "ледяная морось"),
    61: (RAIN, "небольшой дождь"),
    63: (RAIN, "дождь"),
    65: (RAIN, "сильный дождь"),
    66: (FREEZING, "ледяной дождь"),
    67: (FREEZING, "ледяной дождь"),
    71: (SNOW, "небольшой снег"),
    73: (SNOW, "снег"),
    75: (SNOW, "сильный снег"),
    77: (SNOW, "снежная крупа"),
    80: (SHOWERS, "ливень"),
    81: (SHOWERS, "ливень"),
    82: (SHOWERS, "сильный ливень"),
    85: (SNOW, "снегопад"),
    86: (SNOW, "сильный снегопад"),
    95: (THUNDER, "гроза"),
    96: (THUNDER, "гроза с градом"),
    99: (THUNDER, "гроза с градом"),
}


def describe(code: int) -> tuple[str, str]:
    """(icon group, Russian label) for a WMO code; unknown codes read as cloud."""
    return _CODES.get(int(code), (OVERCAST, "—"))


@dataclass(frozen=True)
class Hour:
    when: datetime
    temp: float
    code: int


@dataclass(frozen=True)
class Weather:
    place: str
    observed: datetime
    temp: float
    feels: float
    humidity: int
    wind: float
    code: int
    is_day: bool
    high: float | None
    low: float | None
    sunrise: datetime | None
    sunset: datetime | None
    hourly: tuple[Hour, ...]
    fetched: float

    @property
    def group(self) -> str:
        return describe(self.code)[0]

    @property
    def label(self) -> str:
        return describe(self.code)[1]

    def age(self, now: float | None = None) -> float:
        return (now if now is not None else time.time()) - self.fetched

    def stale(self, now: float | None = None) -> bool:
        return self.age(now) > STALE_AFTER


def _iso(value: str | None) -> datetime | None:
    try:
        return datetime.fromisoformat(value) if value else None
    except ValueError:
        return None


def _pick_hours(hourly: dict, after: datetime, step: int, count: int) -> tuple[Hour, ...]:
    """The next `count` forecast slots, `step` hours apart, starting after now.

    Open-Meteo hands back a flat series beginning at local midnight, so the
    slots before the current hour have to be skipped rather than trusted.
    """
    times = hourly.get("time") or []
    temps = hourly.get("temperature_2m") or []
    codes = hourly.get("weather_code") or []
    out: list[Hour] = []
    start: int | None = None
    for i, stamp in enumerate(times):
        when = _iso(stamp)
        if when is None or when <= after:
            continue
        start = i
        break
    if start is None:
        return ()
    for n in range(count):
        i = start + n * step
        if i >= len(times) or i >= len(temps) or i >= len(codes):
            break
        when = _iso(times[i])
        if when is None or temps[i] is None or codes[i] is None:
            continue
        out.append(Hour(when, float(temps[i]), int(codes[i])))
    return tuple(out)


def fetch(loc: Location, step: int = 3, cells: int = 4,
          timeout: float = 12.0) -> Weather:
    """One blocking request.  Raises on anything that isn't a usable answer."""
    query = urllib.parse.urlencode({
        "latitude": f"{loc.latitude:.4f}",
        "longitude": f"{loc.longitude:.4f}",
        "current": ",".join(CURRENT),
        "hourly": ",".join(HOURLY),
        "daily": ",".join(DAILY),
        "timezone": "auto",
        "forecast_days": 2,
        "wind_speed_unit": "ms",
    })
    req = urllib.request.Request(f"{ENDPOINT}?{query}",
                                 headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        raw = json.loads(resp.read().decode("utf-8"))

    cur = raw["current"]
    daily = raw.get("daily") or {}
    observed = _iso(cur.get("time")) or datetime.now()
    highs = daily.get("temperature_2m_max") or []
    lows = daily.get("temperature_2m_min") or []
    return Weather(
        place=loc.place,
        observed=observed,
        temp=float(cur["temperature_2m"]),
        feels=float(cur.get("apparent_temperature", cur["temperature_2m"])),
        humidity=int(cur.get("relative_humidity_2m") or 0),
        wind=float(cur.get("wind_speed_10m") or 0.0),
        code=int(cur.get("weather_code") or 0),
        is_day=bool(cur.get("is_day", 1)),
        high=float(highs[0]) if highs else None,
        low=float(lows[0]) if lows else None,
        sunrise=_iso((daily.get("sunrise") or [None])[0]),
        sunset=_iso((daily.get("sunset") or [None])[0]),
        hourly=_pick_hours(raw.get("hourly") or {}, observed, step, cells),
        fetched=time.time(),
    )


class WeatherService:
    """Keeps one Weather around, refreshed on a thread.  Never raises at readers."""

    RETRY_MIN = 30.0
    # a tapped panel must not turn into a request per tap, and the touchscreen
    # emits a stray event of its own when the clock first opens it
    MIN_INTERVAL = 20.0

    def __init__(self, cfg: Config) -> None:
        self.cfg = cfg
        self._lock = threading.Lock()
        self._weather: Weather | None = None
        self._location: Location | None = cfg.configured_location()
        self._error: str = ""
        self._wake = threading.Event()
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    # -- readers ----------------------------------------------------------
    def latest(self) -> Weather | None:
        with self._lock:
            return self._weather

    @property
    def location(self) -> Location | None:
        with self._lock:
            return self._location

    @property
    def error(self) -> str:
        with self._lock:
            return self._error

    def stamp(self) -> int:
        """Changes exactly when the drawn content would change."""
        with self._lock:
            if self._weather is not None:
                return int(self._weather.fetched)
            return -1 if self._error else 0

    # -- control ----------------------------------------------------------
    def start(self) -> None:
        if self._thread is not None:
            return
        self._thread = threading.Thread(target=self._run, name="weather", daemon=True)
        self._thread.start()

    def refresh_now(self) -> None:
        """Ask for a fetch out of band; ignored if the reading is still warm."""
        with self._lock:
            recent = self._weather is not None and \
                time.time() - self._weather.fetched < self.MIN_INTERVAL
        if recent:
            log.debug("refresh ignored: last reading is under %.0fs old",
                      self.MIN_INTERVAL)
            return
        self._wake.set()

    def stop(self) -> None:
        self._stop.set()
        self._wake.set()
        if self._thread is not None:
            self._thread.join(timeout=2.0)
            self._thread = None

    # -- worker -----------------------------------------------------------
    def _run(self) -> None:
        delay = 0.0
        while not self._stop.is_set():
            if delay:
                self._wake.wait(delay)
                self._wake.clear()
                if self._stop.is_set():
                    return
            delay = self._tick()

    def _tick(self) -> float:
        """One refresh attempt.  Returns how long to wait before the next."""
        loc = self._location
        if loc is None:
            loc = resolve_location(self.cfg)
            if loc is None:
                self._fail("нет координат")
                return max(self.RETRY_MIN, min(300.0, self.cfg.refresh_seconds))
            with self._lock:
                self._location = loc
        try:
            weather = fetch(loc, self.cfg.hourly_step, self.cfg.hourly_cells)
        except (urllib.error.URLError, OSError, ValueError, KeyError, TypeError) as exc:
            self._fail(str(exc) or exc.__class__.__name__)
            with self._lock:
                had = self._weather is not None
            # a machine that has a reading already can afford to back off; one
            # that has never had one keeps trying at the short interval
            return self.RETRY_MIN * (4 if had else 1)
        with self._lock:
            self._weather = weather
            self._error = ""
        log.info("weather %s: %+.0f°C, %s", weather.place or "?", weather.temp,
                 weather.label)
        return float(self.cfg.refresh_seconds)

    def _fail(self, message: str) -> None:
        with self._lock:
            self._error = message
        log.warning("weather refresh failed: %s", message)
