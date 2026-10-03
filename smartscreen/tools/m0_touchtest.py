#!/usr/bin/env python3
"""M0 calibration: prove the touchscreen works and that its axes match the panel.

Draws four numbered targets plus an EXIT box, then follows your finger with a
crosshair.  Tap the targets in order and compare the printed coordinates with
the labels: if tapping target 1 (top-left) reports something near (0, 0), the
axes need no correction.  Only the patch under the crosshair is redrawn, which
is also a first exercise of the dirty-rect path.
"""

from __future__ import annotations

import sys
import time
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ccdisp.fb import Framebuffer  # noqa: E402
from ccdisp.touch import TouchReader  # noqa: E402

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
PATCH = 32  # crosshair patch size


def background(w: int, h: int) -> tuple[Image.Image, tuple[int, int, int, int]]:
    img = Image.new("RGB", (w, h), (16, 16, 24))
    d = ImageDraw.Draw(img)
    font = ImageFont.truetype(FONT, 12)
    small = ImageFont.truetype(FONT, 10)

    targets = [(0, 0, "1"), (w - 40, 0, "2"), (0, h - 40, "3"), (w - 40, h - 40, "4")]
    for x, y, label in targets:
        d.rectangle([x, y, x + 39, y + 39], outline=(80, 80, 110))
        d.line([x, y, x + 39, y + 39], fill=(60, 60, 90))
        d.line([x + 39, y, x, y + 39], fill=(60, 60, 90))
        d.text((x + 16, y + 13), label, font=font, fill=(200, 200, 255))

    d.text((50, 8), "tap targets 1-4", font=font, fill=(220, 220, 220))
    d.text((50, 24), "then tap EXIT", font=small, fill=(140, 140, 160))

    exit_box = (w // 2 - 40, h // 2 - 16, w // 2 + 40, h // 2 + 16)
    d.rectangle(exit_box, fill=(150, 40, 40))
    d.text((w // 2 - 20, h // 2 - 7), "EXIT", font=font, fill=(255, 255, 255))
    return img, exit_box


def crosshair(bg: Image.Image, x: int, y: int) -> tuple[Image.Image, int, int]:
    """Return a PATCH-sized tile of the background with a crosshair drawn on it."""
    w, h = bg.size
    px = max(0, min(w - PATCH, x - PATCH // 2))
    py = max(0, min(h - PATCH, y - PATCH // 2))
    tile = bg.crop((px, py, px + PATCH, py + PATCH)).copy()
    d = ImageDraw.Draw(tile)
    cx, cy = x - px, y - py
    d.line([cx - 10, cy, cx + 10, cy], fill=(0, 255, 120))
    d.line([cx, cy - 10, cx, cy + 10], fill=(0, 255, 120))
    d.ellipse([cx - 5, cy - 5, cx + 5, cy + 5], outline=(0, 255, 120))
    return tile, px, py


def main() -> int:
    # Stay line-buffered when stdout is a file, so a run watched from another
    # process shows touches as they happen instead of all at once at exit.
    sys.stdout.reconfigure(line_buffering=True)
    with Framebuffer() as fb, TouchReader() as ts:
        print(f"panel : {fb.info.path} {fb.width}x{fb.height}")
        print(f"touch : {ts.path}")
        print("waiting for touches (Ctrl-C to quit)\n")

        bg, exit_box = background(fb.width, fb.height)
        fb.blit(bg)
        last: tuple[int, int] | None = None
        deadline = time.monotonic() + 120

        while time.monotonic() < deadline:
            if not ts.wait(timeout=0.5):
                continue
            for ev in ts.read():
                print(f"  {ev.kind:<4} x={ev.x:>4} y={ev.y:>4}")
                if ev.kind == "up":
                    x0, y0, x1, y1 = exit_box
                    if x0 <= ev.x <= x1 and y0 <= ev.y <= y1:
                        print("\nEXIT tapped — touch mapping confirmed.")
                        fb.fill((0, 0, 0))
                        return 0
                    continue
                if last is not None:
                    px, py = last
                    fb.blit(bg.crop((px, py, px + PATCH, py + PATCH)), px, py)
                tile, px, py = crosshair(bg, ev.x, ev.y)
                fb.blit(tile, px, py)
                last = (px, py)

        print("\ntimed out after 120s")
        fb.fill((0, 0, 0))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\ninterrupted")
