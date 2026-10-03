#!/usr/bin/env python3
"""M0 calibration: prove we can drive the panel and settle the pixel order.

Draws the same three colour bars twice, packed both ways.  Whichever half has
the bar labelled RED actually looking red is the correct layout for this panel.
Run it, look at the screen, and report which half is right.
"""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ccdisp.fb import Framebuffer  # noqa: E402

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
BARS = [("RED", (255, 0, 0)), ("GREEN", (0, 255, 0)), ("BLUE", (0, 0, 255))]


def half(width: int, height: int, caption: str) -> Image.Image:
    img = Image.new("RGB", (width, height), (0, 0, 0))
    d = ImageDraw.Draw(img)
    small = ImageFont.truetype(FONT, 11)
    big = ImageFont.truetype(FONT, 13)

    d.text((4, 2), caption, font=big, fill=(255, 255, 255))
    top, bar_h = 20, height - 20 - 16
    bar_w = width // 3
    for i, (label, color) in enumerate(BARS):
        x = i * bar_w
        d.rectangle([x, top, x + bar_w - 2, top + bar_h], fill=color)
        d.text((x + 6, top + bar_h + 2), label, font=small, fill=(255, 255, 255))
    return img


def main() -> int:
    with Framebuffer() as fb:
        i = fb.info
        print(f"device      : {i.path}")
        print(f"geometry    : {i.width}x{i.height} @ {i.bpp}bpp, stride {i.stride}")
        print(f"red         : offset {i.red.offset} length {i.red.length}")
        print(f"green       : offset {i.green.offset} length {i.green.length}")
        print(f"blue        : offset {i.blue.offset} length {i.blue.length}")
        print(f"driver says : {'BGR565 (red in low bits)' if i.red_first else 'RGB565'}")

        h = fb.height // 2
        fb.blit(half(fb.width, h, "A  red@bit0  (BGR565)"), 0, 0, red_first=True)
        fb.blit(half(fb.width, fb.height - h, "B  red@bit11 (RGB565)"), 0, h,
                red_first=False)

    print("\nLook at the panel: in which half is the bar labelled RED actually red?")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
