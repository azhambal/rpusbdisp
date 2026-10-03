#!/usr/bin/env python3
"""Regenerate the README pictures: every page, the sky through a day, a swipe.

Runs entirely off-panel: pages draw into images, the pager composes the swipe
frames exactly as it does on the hardware.  Weather is fetched live, so give
it the same proxy the service uses if the machine needs one.

  python3 tools/screenshots.py               # writes into screenshots/
  python3 tools/screenshots.py --out /tmp/x
"""

from __future__ import annotations

import argparse
import sys
import time
from datetime import datetime, timedelta
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
from ccdisp.clock import HOME, PAGE_NAMES, build_pages  # noqa: E402
from ccdisp.config import Config, known_location  # noqa: E402
from ccdisp.pager import Pager  # noqa: E402
from ccdisp.sky import Sky, SunDay  # noqa: E402

SIZE = (320, 240)
SCALE = 2            # whole pixels only: the panel is pixel art, keep it crisp
BEZEL = 22           # px around the screen, after scaling
BEZEL_FILL = (12, 12, 16, 255)
BEZEL_EDGE = (64, 64, 78, 255)


def device(screen: Image.Image, scale: int = SCALE, bezel: int = BEZEL) -> Image.Image:
    """The screen inside a rounded dark bezel, on a transparent background,
    so it sits well on both GitHub themes."""
    screen = screen.convert("RGB").resize((screen.width * scale, screen.height * scale),
                                          Image.Resampling.NEAREST)
    w, h = screen.width + 2 * bezel, screen.height + 2 * bezel
    ss = 3  # the bezel's curves are drawn large and shrunk, for smooth corners
    shape = Image.new("RGBA", (w * ss, h * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(shape)
    d.rounded_rectangle([0, 0, w * ss - 1, h * ss - 1], radius=26 * ss,
                        fill=BEZEL_FILL, outline=BEZEL_EDGE, width=2 * ss)
    out = shape.reduce(ss)
    mask = Image.new("L", (screen.width * ss, screen.height * ss), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, mask.width - 1, mask.height - 1],
                                           radius=6 * ss, fill=255)
    out.paste(screen, (bezel, bezel), mask.reduce(ss))
    return out


def wait_for_weather(face, seconds: float) -> None:
    if face.weather is None:
        return
    deadline = time.monotonic() + seconds
    while face.weather.latest() is None and time.monotonic() < deadline:
        time.sleep(0.2)
    if face.weather.latest() is None:
        print(f"no weather: {face.weather.error or 'timed out'}", file=sys.stderr)


def stills(pages, now: datetime, out: Path) -> None:
    for name, page in zip(PAGE_NAMES, pages):
        page.invalidate()
        device(page.frame(now)).save(out / f"{name}.png", optimize=True)
        print(f"wrote {out / name}.png")


def sky_day(cfg: Config, now: datetime, out: Path) -> None:
    """One page, four moments: dawn, noon, sunset, night."""
    loc = known_location(cfg)
    if loc is None:
        print("no location known — skipping sky-day.png", file=sys.stderr)
        return
    sky = Sky(SIZE, lambda: loc)
    day = SunDay(now.date(), loc.latitude, loc.longitude)
    base = datetime(now.year, now.month, now.day)
    moments = [base + timedelta(hours=12, minutes=30), base + timedelta(hours=23, minutes=30)]
    if day.rise and day.set:
        rise, sunset = day.rise.replace(tzinfo=None), day.set.replace(tzinfo=None)
        moments = [rise - timedelta(minutes=20), moments[0],
                   sunset + timedelta(minutes=8), moments[1]]
    gap = 16
    tiles = [device(sky.render(t, loc), scale=1, bezel=12) for t in moments]
    strip = Image.new("RGBA", (sum(t.width for t in tiles) + gap * (len(tiles) - 1),
                               tiles[0].height), (0, 0, 0, 0))
    x = 0
    for tile in tiles:
        strip.paste(tile, (x, 0))
        x += tile.width + gap
    strip.save(out / "sky-day.png", optimize=True)
    print(f"wrote {out / 'sky-day.png'}")


def swipe_gif(pages, now: datetime, out: Path) -> None:
    """A tour: home, left to the dial, back, then right to the end and home."""
    pager = Pager(SIZE, pages, HOME)
    pager.go(HOME)
    width = SIZE[0]
    frames: list[Image.Image] = []
    durations: list[int] = []

    def hold(ms: int) -> None:
        pager.current.invalidate()
        still = pager.current.frame(now)
        pager._base = still
        img = still.copy()
        pager._draw_dots(img, float(pager.index))
        frames.append(device(img))       # landing: the dots are still up
        durations.append(500)
        frames.append(device(still))
        durations.append(ms)

    def turn(step: int) -> None:
        """step -1: the page on the left slides in; +1: the one on the right."""
        steps = 9
        for i in range(1, steps):
            t = i / steps
            ease = t * t * (3 - 2 * t)  # finger drag then settle, as one curve
            offset = -step * width * ease
            frames.append(device(pager.compose(offset, pager.index + step, now, dots=True)))
            durations.append(40)
        pager.go(pager.index + step)

    hold(2200)
    for step in (-1, +1, +1, +1, -1, -1):
        turn(step)
        hold(1800)

    # GIF wants a palette per frame; transparency in an animated GIF goes
    # ragged at the rounded corners, so flatten onto GitHub's dark canvas
    flat = []
    for frame in frames:
        bg = Image.new("RGBA", frame.size, (13, 17, 23, 255))
        bg.alpha_composite(frame)
        flat.append(bg.convert("RGB").quantize(colors=128, method=Image.Quantize.MEDIANCUT,
                                               dither=Image.Dither.NONE))
    flat[0].save(out / "swipe.gif", save_all=True, append_images=flat[1:],
                 duration=durations, loop=0, optimize=True, disposal=1)
    print(f"wrote {out / 'swipe.gif'} ({len(flat)} frames)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=HERE / "screenshots")
    parser.add_argument("--wait", type=float, default=20.0,
                        help="seconds to wait for the first weather reading")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    cfg = Config.load()
    pages = build_pages(SIZE, cfg)
    face = pages[HOME]
    face.start()
    try:
        wait_for_weather(face, args.wait)
        now = datetime.now()
        stills(pages, now, args.out)
        sky_day(cfg, now, args.out)
        swipe_gif(pages, now, args.out)
    finally:
        face.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
