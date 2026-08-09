"""Card rendering for a 320x240 panel.

Everything is laid out in absolute pixels because there is no room for anything
adaptive.  render() returns the full-screen image plus the hit regions, so the
daemon never has to know how the layout was computed.
"""

from __future__ import annotations

import textwrap
from dataclasses import dataclass

from PIL import Image, ImageDraw, ImageFont

from .proto import Card, LAYOUT_LIST, LAYOUT_NOTICE

MONO = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
SANS = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
SANS_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

BG = (18, 18, 24)
HEADER_BG = (30, 30, 42)
FG = (232, 232, 238)
DIM = (140, 140, 160)
RULE = (60, 60, 78)

COLORS = {
    "green": ((34, 110, 60), (60, 190, 100)),
    "yellow": ((120, 96, 24), (220, 180, 60)),
    "red": ((130, 44, 44), (225, 90, 90)),
    "neutral": ((52, 52, 68), (170, 170, 190)),
}
RISK_BORDER = {"high": (225, 90, 90), "medium": (220, 180, 60)}

HEADER_H = 22
BUTTON_GAP = 3


@dataclass(frozen=True)
class HitRegion:
    x0: int
    y0: int
    x1: int
    y1: int
    button_id: str

    def contains(self, x: int, y: int) -> bool:
        return self.x0 <= x <= self.x1 and self.y0 <= y <= self.y1


class Fonts:
    """Loaded once; PIL font objects are not cheap to build."""

    def __init__(self) -> None:
        self.mono = ImageFont.truetype(MONO, 12)
        self.mono_small = ImageFont.truetype(MONO, 10)
        self.sans = ImageFont.truetype(SANS, 12)
        self.sans_small = ImageFont.truetype(SANS, 10)
        self.title = ImageFont.truetype(SANS_BOLD, 14)
        self.button = ImageFont.truetype(SANS_BOLD, 15)
        # advance width of the monospaced face, for wrapping body text
        self.mono_w = int(self.mono.getlength("M")) or 7


_fonts: Fonts | None = None


def fonts() -> Fonts:
    global _fonts
    if _fonts is None:
        _fonts = Fonts()
    return _fonts


def shorten_middle(text: str, limit: int) -> str:
    """Collapse the middle of an over-long single line, keeping both ends."""
    if len(text) <= limit:
        return text
    keep = limit - 1
    head = (keep + 1) // 2
    return text[:head] + "…" + text[len(text) - (keep - head):]


def _fit(d: ImageDraw.ImageDraw, text: str, font, max_px: float) -> str:
    """Shorten until the text actually measures under max_px.

    Estimating from a character count works for the monospaced body but not for
    button labels: the bold face is proportional and Cyrillic runs noticeably
    wider than the estimate, so labels spilled past the button edges.
    """
    if d.textlength(text, font=font) <= max_px:
        return text
    lo, hi = 1, len(text)
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if d.textlength(shorten_middle(text, mid), font=font) <= max_px:
            lo = mid
        else:
            hi = mid - 1
    return shorten_middle(text, lo)


def _draw_header(d: ImageDraw.ImageDraw, card: Card, width: int,
                 queue: tuple[int, int], remaining: float | None) -> None:
    f = fonts()
    d.rectangle([0, 0, width, HEADER_H - 1], fill=HEADER_BG)

    left = card.session_label or "claude"
    if card.mode:
        left = f"{left}  {card.mode}"
    d.text((5, 5), shorten_middle(left, 26), font=f.sans_small, fill=FG)

    right = ""
    pos, total = queue
    if total > 1:
        right = f"[{pos}/{total}]"
    if remaining is not None:
        clock = f"{int(remaining) // 60}:{int(remaining) % 60:02d}"
        right = f"{right}  {clock}" if right else clock
    if right:
        w = d.textlength(right, font=f.sans_small)
        d.text((width - w - 5, 5), right, font=f.sans_small, fill=DIM)


def _draw_button(d: ImageDraw.ImageDraw, box: tuple[int, int, int, int],
                 label: str, sub: str, color: str, pressed: bool) -> None:
    f = fonts()
    fill, accent = COLORS.get(color, COLORS["neutral"])
    if pressed:
        fill = tuple(min(255, c + 45) for c in fill)
    x0, y0, x1, y1 = box
    d.rectangle(box, fill=fill)
    d.rectangle([x0, y0, x1, y0], fill=accent)

    box_w = x1 - x0
    text = _fit(d, label, f.button, box_w - 12)
    tw = d.textlength(text, font=f.button)
    ty = (y0 + y1) // 2 - (13 if sub else 9)
    d.text((x0 + (box_w - tw) / 2, ty), text, font=f.button, fill=FG)
    if sub:
        stext = _fit(d, sub, f.sans_small, box_w - 12)
        sw = d.textlength(stext, font=f.sans_small)
        d.text((x0 + (box_w - sw) / 2, ty + 19), stext, font=f.sans_small, fill=(225, 225, 235))


def _plural_lines(n: int) -> str:
    """Russian needs three forms, and 'ещё 3 строк' reads as a bug."""
    tail = n % 100
    if 11 <= tail <= 14:
        return "строк"
    return {1: "строка", 2: "строки", 3: "строки", 4: "строки"}.get(n % 10, "строк")


def _wrap_body(card: Card, cols: int, max_lines: int) -> list[str]:
    lines: list[str] = []
    for para in (card.body or "").splitlines() or [""]:
        lines.extend(textwrap.wrap(para, cols) or [""])
    if len(lines) > max_lines:
        hidden = len(lines) - (max_lines - 1)
        lines = lines[:max_lines - 1] + [f"… ещё {hidden} {_plural_lines(hidden)}"]
    return lines


def render(card: Card, size: tuple[int, int], queue: tuple[int, int] = (1, 1),
           remaining: float | None = None,
           pressed: str | None = None) -> tuple[Image.Image, list[HitRegion]]:
    """Draw a card and report where its buttons ended up."""
    width, height = size
    f = fonts()
    img = Image.new("RGB", size, BG)
    d = ImageDraw.Draw(img)

    _draw_header(d, card, width, queue, remaining)
    border = RISK_BORDER.get(card.risk)
    if border:
        d.rectangle([0, HEADER_H, width - 1, height - 1], outline=border)

    hits: list[HitRegion] = []
    buttons = card.buttons

    if card.layout == LAYOUT_NOTICE or not buttons:
        _draw_notice(d, card, size)
        return img, hits

    if card.layout == LAYOUT_LIST:
        # Buttons hang off the bottom edge, the same place they sit on a
        # permission card, so the thumb lands consistently between layouts.
        count = len(buttons)
        gaps = BUTTON_GAP * (count - 1)
        # The title carries the question itself, so it gets up to two lines and
        # the buttons give up the height: a list of answers means nothing when
        # the question it answers has been squeezed off the screen.
        title_lines = textwrap.wrap(card.title, 30)[:2] or [""]
        title_h = len(title_lines) * 18 + 4
        bh = min(44, (height - HEADER_H - 4 - title_h - gaps) // count)
        buttons_top = height - (bh * count + gaps) - 4

        top = HEADER_H + 4
        for i, line in enumerate(title_lines):
            d.text((8, top + i * 18), line, font=f.title, fill=FG)
        top += title_h

        cols = max(10, (width - 16) // f.mono_w)
        max_lines = max(0, (buttons_top - top - 2) // 13)
        if max_lines:
            for i, line in enumerate(_wrap_body(card, cols, max_lines)):
                d.text((8, top + i * 13), line, font=f.mono_small, fill=DIM)

        top = buttons_top
        for b in buttons:
            box = (6, top, width - 7, top + bh - 1)
            _draw_button(d, box, b.label, b.sub, b.color, pressed == b.id)
            hits.append(HitRegion(*box, b.id))
            top += bh + BUTTON_GAP
        return img, hits

    # LAYOUT_PERMISSION: the first two buttons share a row, any further ones
    # get a full-width row of their own because rule patterns need the width.
    wide = buttons[2:]
    wide_h = 40 if wide else 0
    row_h = 58
    buttons_top = height - (row_h + len(wide) * (wide_h + BUTTON_GAP))

    body_top = HEADER_H + 4
    d.text((8, body_top), shorten_middle(card.title, 34), font=f.title, fill=FG)
    d.line([8, body_top + 20, width - 9, body_top + 20], fill=RULE)

    text_top = body_top + 25
    cols = max(10, (width - 16) // f.mono_w)
    max_lines = max(1, (buttons_top - text_top - 4) // 14)
    for i, line in enumerate(_wrap_body(card, cols, max_lines)):
        d.text((8, text_top + i * 14), line, font=f.mono, fill=FG)
    if card.detail:
        last = text_top + len(_wrap_body(card, cols, max_lines)) * 14
        if last + 12 < buttons_top:
            d.text((8, last + 2), shorten_middle(card.detail, cols),
                   font=f.sans_small, fill=DIM)

    half = (width - BUTTON_GAP) // 2
    for i, b in enumerate(buttons[:2]):
        x0 = i * (half + BUTTON_GAP)
        box = (x0, buttons_top, x0 + half - 1, buttons_top + row_h - 1)
        _draw_button(d, box, b.label, b.sub, b.color, pressed == b.id)
        hits.append(HitRegion(*box, b.id))

    top = buttons_top + row_h + BUTTON_GAP
    for b in wide:
        box = (0, top, width - 1, top + wide_h - 1)
        _draw_button(d, box, b.label, b.sub, b.color, pressed == b.id)
        hits.append(HitRegion(*box, b.id))
        top += wide_h + BUTTON_GAP

    return img, hits


def _draw_notice(d: ImageDraw.ImageDraw, card: Card, size: tuple[int, int]) -> None:
    f = fonts()
    width, height = size
    lines = [card.title] + textwrap.wrap(card.body or "", 34)
    total = len(lines) * 20
    y = HEADER_H + (height - HEADER_H - total) // 2
    for i, line in enumerate(lines):
        font = f.title if i == 0 else f.sans
        color = FG if i == 0 else DIM
        w = d.textlength(line, font=font)
        d.text(((width - w) / 2, y), line, font=font, fill=color)
        y += 20


def idle_screen(size: tuple[int, int], label: str, status: str,
                stats: str = "") -> Image.Image:
    """What the panel shows when nothing is waiting on an answer."""
    f = fonts()
    width, height = size
    img = Image.new("RGB", size, BG)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, width, HEADER_H - 1], fill=HEADER_BG)
    d.text((5, 5), shorten_middle(label, 30), font=f.sans_small, fill=DIM)

    w = d.textlength(status, font=f.title)
    d.text(((width - w) / 2, height / 2 - 16), status, font=f.title, fill=FG)
    if stats:
        w = d.textlength(stats, font=f.sans_small)
        d.text(((width - w) / 2, height / 2 + 10), stats, font=f.sans_small, fill=DIM)
    return img
