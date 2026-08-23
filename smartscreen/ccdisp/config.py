"""Where the clock face gets its location and its knobs.

Coordinates live in ~/.config/ccdisp/clock.json.  Without that file we ask an
IP geolocation service exactly once and cache the answer next to it: a fresh
install shows the right city without anyone editing JSON, and a VPN hop later
cannot make the panel wander to another country every ten minutes.
"""

from __future__ import annotations

import json
import logging
import os
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

log = logging.getLogger("ccdisp.config")

USER_AGENT = "ccdisp-clock/1.0 (+https://github.com/azhambal/rpusbdisp)"

# Tried in order.  ipapi.co speaks https but rate-limits hard; ip-api.com is
# plain http and generous.  Either way this runs once and the answer is cached.
GEO_ENDPOINTS = (
    ("https://ipapi.co/json/", ("latitude", "longitude", "city")),
    ("http://ip-api.com/json/", ("lat", "lon", "city")),
)


def config_path() -> Path:
    override = os.environ.get("CCDISP_CONFIG")
    if override:
        return Path(override).expanduser()
    base = os.environ.get("XDG_CONFIG_HOME") or "~/.config"
    return Path(base).expanduser() / "ccdisp" / "clock.json"


def cache_path() -> Path:
    base = os.environ.get("XDG_CACHE_HOME") or "~/.cache"
    return Path(base).expanduser() / "ccdisp" / "location.json"


@dataclass
class Location:
    latitude: float
    longitude: float
    place: str = ""


@dataclass
class Config:
    """Everything the idle face reads.  All fields optional in the JSON."""

    latitude: float | None = None
    longitude: float | None = None
    place: str = ""
    weather: bool = True
    refresh_seconds: int = 600
    blink_colon: bool = True
    show_date: bool = True
    hourly_step: int = 3          # hours between the forecast cells
    hourly_cells: int = 4
    geolocate: bool = True        # ask the network when no coordinates given
    extra: dict = field(default_factory=dict)

    def __post_init__(self) -> None:
        # clamp here rather than in load(): nothing constructed in code should
        # be able to poll a public API every five seconds either
        self.refresh_seconds = max(120, int(self.refresh_seconds))
        self.hourly_cells = max(0, min(5, int(self.hourly_cells)))
        self.hourly_step = max(1, int(self.hourly_step))

    @classmethod
    def load(cls, path: Path | None = None) -> "Config":
        path = path or config_path()
        try:
            raw = json.loads(path.read_text(encoding="utf-8"))
        except FileNotFoundError:
            return cls()
        except (OSError, ValueError) as exc:
            log.warning("%s: %s — using defaults", path, exc)
            return cls()
        known = {f for f in cls.__dataclass_fields__ if f != "extra"}
        kwargs = {k: v for k, v in raw.items() if k in known}
        extra = {k: v for k, v in raw.items() if k not in known and not k.startswith("_")}
        return cls(**kwargs, extra=extra)

    def configured_location(self) -> Location | None:
        if self.latitude is None or self.longitude is None:
            return None
        return Location(float(self.latitude), float(self.longitude), self.place)


def _read_cached_location() -> Location | None:
    try:
        raw = json.loads(cache_path().read_text(encoding="utf-8"))
        return Location(float(raw["latitude"]), float(raw["longitude"]),
                        str(raw.get("place", "")))
    except (OSError, ValueError, KeyError, TypeError):
        return None


def _write_cached_location(loc: Location) -> None:
    path = cache_path()
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps({"latitude": loc.latitude,
                                    "longitude": loc.longitude,
                                    "place": loc.place}, ensure_ascii=False),
                        encoding="utf-8")
    except OSError as exc:
        log.debug("could not cache location: %s", exc)


def geolocate(timeout: float = 8.0) -> Location | None:
    """Best-effort location from our public IP.  None if nobody answers."""
    for url, (lat_key, lon_key, place_key) in GEO_ENDPOINTS:
        req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                raw = json.loads(resp.read().decode("utf-8"))
            lat, lon = float(raw[lat_key]), float(raw[lon_key])
        except (urllib.error.URLError, OSError, ValueError, KeyError, TypeError) as exc:
            log.debug("geolocation via %s failed: %s", url, exc)
            continue
        loc = Location(lat, lon, str(raw.get(place_key) or ""))
        log.info("located by IP: %s (%.3f, %.3f)", loc.place or "?", lat, lon)
        return loc
    return None


def resolve_location(cfg: Config) -> Location | None:
    """Config first, then the cached lookup, then the network."""
    configured = cfg.configured_location()
    if configured is not None:
        return configured
    cached = _read_cached_location()
    if cached is not None:
        # a configured place name still wins over whatever the service called it
        return Location(cached.latitude, cached.longitude, cfg.place or cached.place)
    if not cfg.geolocate:
        return None
    found = geolocate()
    if found is None:
        return None
    _write_cached_location(found)
    return Location(found.latitude, found.longitude, cfg.place or found.place)
