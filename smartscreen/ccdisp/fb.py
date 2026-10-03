"""Framebuffer backend for the RoboPeak USB display.

Finds the device by its sysfs name instead of hardcoding /dev/fbN, reads the
real pixel layout out of FBIOGET_VSCREENINFO (this panel reports red at bit 0
and blue at bit 11, i.e. BGR565, not the usual RGB565), and blits Pillow images
into the mmapped region.  The kernel driver uses fb_defio, so writes to the
mapping are picked up and pushed over USB on their own; we only have to keep
the written area as small as possible.
"""

from __future__ import annotations

import ctypes
import fcntl
import mmap
import os
import struct
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageChops

FBIOGET_VSCREENINFO = 0x4600
FBIOGET_FSCREENINFO = 0x4602

DEFAULT_FB_NAME = "rpusbdisp-fb"

# fb_var_screeninfo, as far as we care about it: 8 leading __u32 followed by
# four fb_bitfield (offset, length, msb_right) triples for r/g/b/transp.
_VAR_PREFIX = struct.Struct("=20I")

# 565 packing tables for Framebuffer.pack(), one 8-bit channel in, one byte's
# share out.  Each pair adds up to at most 255, so ImageChops.add never clips.
_HIGH5 = [v & 0xF8 for v in range(256)]            # bits 15..11 -> high byte 7..3
_G_HIGH3 = [v >> 5 for v in range(256)]            # bits 10..8  -> high byte 2..0
_G_LOW3 = [(v >> 2 & 7) << 5 for v in range(256)]  # bits 7..5   -> low byte 7..5
_LOW5 = [v >> 3 for v in range(256)]               # bits 4..0   -> low byte 4..0


@dataclass(frozen=True)
class Bitfield:
    offset: int
    length: int
    msb_right: int


@dataclass(frozen=True)
class FbInfo:
    path: str
    width: int
    height: int
    bpp: int
    stride: int
    red: Bitfield
    green: Bitfield
    blue: Bitfield

    @property
    def red_first(self) -> bool:
        """True when red sits in the low bits (BGR565), as on this panel."""
        return self.red.offset < self.blue.offset


class FbNotFound(Exception):
    pass


def find_framebuffer(name: str = DEFAULT_FB_NAME) -> str:
    """Return /dev/fbN for the graphics device whose sysfs name matches."""
    for entry in sorted(Path("/sys/class/graphics").glob("fb*")):
        name_file = entry / "name"
        try:
            if name_file.read_text().strip() == name:
                return f"/dev/{entry.name}"
        except OSError:
            continue
    raise FbNotFound(f"no framebuffer named {name!r} under /sys/class/graphics")


def _read_var_screeninfo(fd: int) -> tuple[int, int, int, Bitfield, Bitfield, Bitfield]:
    buf = ctypes.create_string_buffer(160)
    fcntl.ioctl(fd, FBIOGET_VSCREENINFO, buf)
    f = _VAR_PREFIX.unpack_from(buf.raw)
    xres, yres, bpp = f[0], f[1], f[6]
    red = Bitfield(f[8], f[9], f[10])
    green = Bitfield(f[11], f[12], f[13])
    blue = Bitfield(f[14], f[15], f[16])
    return xres, yres, bpp, red, green, blue


def _read_line_length(fd: int) -> int:
    # fb_fix_screeninfo: char id[16], unsigned long smem_start, __u32 smem_len,
    # type, type_aux, visual, __u16 xpanstep, ypanstep, ywrapstep(+pad),
    # __u32 line_length.
    buf = ctypes.create_string_buffer(80)
    fcntl.ioctl(fd, FBIOGET_FSCREENINFO, buf)
    (line_length,) = struct.unpack_from("=I", buf.raw, 16 + 8 + 4 * 4 + 2 * 3 + 2)
    return line_length


class Framebuffer:
    """A mmapped framebuffer that accepts Pillow images."""

    def __init__(self, name: str = DEFAULT_FB_NAME, path: str | None = None):
        self.path = path or find_framebuffer(name)
        self._fd = os.open(self.path, os.O_RDWR)
        try:
            xres, yres, bpp, red, green, blue = _read_var_screeninfo(self._fd)
            stride = _read_line_length(self._fd)
        except OSError:
            os.close(self._fd)
            raise
        if bpp != 16:
            os.close(self._fd)
            raise NotImplementedError(f"{self.path}: only 16bpp is supported, got {bpp}")

        self.info = FbInfo(self.path, xres, yres, bpp, stride, red, green, blue)
        self._size = stride * yres
        self._map = mmap.mmap(self._fd, self._size, mmap.MAP_SHARED,
                              mmap.PROT_READ | mmap.PROT_WRITE)
        # what we last wrote across the whole panel, for blit_changed(); None
        # whenever a partial write left us unsure what is actually on screen
        self._last: Image.Image | None = None

    # -- properties -------------------------------------------------------
    @property
    def width(self) -> int:
        return self.info.width

    @property
    def height(self) -> int:
        return self.info.height

    @property
    def size(self) -> tuple[int, int]:
        return self.info.width, self.info.height

    # -- pixel packing ----------------------------------------------------
    def pack(self, image: Image.Image, red_first: bool | None = None) -> bytes:
        """Convert an RGB image to this panel's little-endian 16-bit format.

        Pillow 12 dropped the "BGR;16" mode and has no raw 565 packer, so we
        build the two bytes of each pixel as "L" bands through lookup tables
        and let "LA" interleave them (low byte first) — still all at C speed.
        This panel wants red in the low bits, so blue goes in the high five.
        `red_first` overrides the layout the driver reported, which the M0
        calibration tool uses to show both orders side by side.
        """
        if image.mode != "RGB":
            image = image.convert("RGB")
        r, g, b = image.split()
        hi, lo = (b, r) if (self.info.red_first if red_first is None else red_first) else (r, b)
        high = ImageChops.add(hi.point(_HIGH5), g.point(_G_HIGH3))
        low = ImageChops.add(g.point(_G_LOW3), lo.point(_LOW5))
        return Image.merge("LA", (low, high)).tobytes()

    # -- drawing ----------------------------------------------------------
    def blit(self, image: Image.Image, x: int = 0, y: int = 0,
             red_first: bool | None = None) -> None:
        """Copy an image onto the panel at (x, y). Only those rows are touched."""
        w, h = image.size
        if x < 0 or y < 0 or x + w > self.width or y + h > self.height:
            raise ValueError(f"blit {w}x{h} at ({x},{y}) does not fit {self.size}")
        self._last = (image.convert("RGB") if (x, y) == (0, 0) and image.size == self.size
                      else None)
        data = self.pack(image, red_first)
        row_bytes = w * 2
        stride = self.info.stride
        for row in range(h):
            dst = (y + row) * stride + x * 2
            src = row * row_bytes
            self._map[dst:dst + row_bytes] = data[src:src + row_bytes]

    def blit_changed(self, image: Image.Image,
                     red_first: bool | None = None) -> int:
        """Blit a full-screen image, writing only the part of it that moved.

        The panel is driven by fb_defio, which pushes every page we dirty over
        USB.  A clock redrawing once a second has no business re-sending 150 KB
        of identical pixels, so we diff against the last frame and touch only
        its bounding box.  Returns how many pixels were written.
        """
        if image.mode != "RGB":
            image = image.convert("RGB")
        if image.size != self.size:
            raise ValueError(f"blit_changed expects {self.size}, got {image.size}")
        if self._last is None:
            self.blit(image, red_first=red_first)
            return self.width * self.height
        bbox = ImageChops.difference(self._last, image).getbbox()
        if bbox is None:
            return 0
        x0, y0, x1, y1 = bbox
        self.blit(image.crop(bbox), x0, y0, red_first=red_first)
        self._last = image.copy()
        return (x1 - x0) * (y1 - y0)

    def fill(self, color: tuple[int, int, int]) -> None:
        self.blit(Image.new("RGB", self.size, color))

    def close(self) -> None:
        if getattr(self, "_map", None) is not None:
            self._map.close()
            self._map = None
        if getattr(self, "_fd", None) is not None:
            os.close(self._fd)
            self._fd = None

    def __enter__(self) -> "Framebuffer":
        return self

    def __exit__(self, *exc) -> None:
        self.close()
