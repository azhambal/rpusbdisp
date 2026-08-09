"""ccdispd — owns the panel, serialises cards from every Claude Code session.

Single-threaded selector loop over three kinds of readiness: the listening
socket, each connected hook, and the touchscreen.  Hooks are short-lived and
block on their answer, so a connection dropping means the tool call went away
and its card should come off the screen.
"""

from __future__ import annotations

import errno
import json
import logging
import os
import selectors
import signal
import socket
import time
from dataclasses import dataclass, field
from pathlib import Path

from PIL import Image

from . import ui
from .fb import FbNotFound, Framebuffer
from .proto import (Answer, Card, SRC_CANCEL, SRC_TIMEOUT, SRC_TOUCH, encode)
from .touch import TouchNotFound, TouchReader

log = logging.getLogger("ccdispd")

TAP_SLOP = 24  # touch reports quantise to ~5px; allow a finger to wobble


def socket_path() -> Path:
    runtime = os.environ.get("XDG_RUNTIME_DIR") or f"/run/user/{os.getuid()}"
    return Path(runtime) / "ccdisp.sock"


@dataclass
class Pending:
    card: Card
    conn: socket.socket
    started: float
    deadline: float
    answered: bool = False
    buf: bytes = b""


@dataclass
class Stats:
    allowed: int = 0
    denied: int = 0

    def line(self) -> str:
        if not (self.allowed or self.denied):
            return ""
        return f"✓ {self.allowed}   ✗ {self.denied}"


class Daemon:
    def __init__(self, idle_label: str = "claude code") -> None:
        self.fb = Framebuffer()
        self.touch = TouchReader()
        self.sel = selectors.DefaultSelector()
        self.queue: list[Pending] = []
        # partial lines per connection, keyed by fd: socket objects use
        # __slots__, so the buffer cannot be stashed on the socket itself
        self._buffers: dict[int, bytes] = {}
        self.stats = Stats()
        self.idle_label = idle_label
        self.idle_status = "готов"
        self._pressed: str | None = None
        self._press_origin: tuple[int, int] | None = None
        self._hits: list[ui.HitRegion] = []
        self._shown: tuple[str, int, str | None] | None = None
        self._running = True

        self.path = socket_path()
        if self.path.exists():
            self.path.unlink()
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server.bind(str(self.path))
        os.chmod(self.path, 0o600)
        self.server.listen(8)
        self.server.setblocking(False)

        self.sel.register(self.server, selectors.EVENT_READ, self._on_accept)
        self.sel.register(self.touch.fileno(), selectors.EVENT_READ, self._on_touch)

    # -- lifecycle --------------------------------------------------------
    def close(self) -> None:
        for pending in list(self.queue):
            self._respond(pending, "", SRC_CANCEL)
        try:
            self.fb.fill((0, 0, 0))
        except OSError:
            pass
        self.sel.close()
        self.server.close()
        self.touch.close()
        self.fb.close()
        self.path.unlink(missing_ok=True)

    def stop(self, *_args) -> None:
        self._running = False

    def run(self) -> None:
        log.info("listening on %s, panel %s", self.path, self.fb.info.path)
        self._render()
        while self._running:
            timeout = 0.25 if self.queue else 1.0
            for key, _mask in self.sel.select(timeout):
                try:
                    key.data(key.fileobj)
                except Exception:
                    # one misbehaving hook must not take the panel away from
                    # every other session
                    log.exception("handler failed for %r", key.fileobj)
            self._expire()
            self._render()

    # -- socket -----------------------------------------------------------
    def _on_accept(self, server: socket.socket) -> None:
        try:
            conn, _ = server.accept()
        except OSError:
            return
        conn.setblocking(False)
        self.sel.register(conn, selectors.EVENT_READ, self._on_client_data)

    def _on_client_data(self, conn: socket.socket) -> None:
        pending = self._pending_for(conn)
        try:
            chunk = conn.recv(65536)
        except (BlockingIOError, InterruptedError):
            return
        except OSError:
            chunk = b""

        if not chunk:
            # the hook went away: its tool call is gone, drop the card
            if pending is not None:
                self._drop(pending, SRC_CANCEL)
            else:
                self._forget(conn)
            return

        if pending is not None:
            # a card is already on screen for this connection; ignore chatter
            return

        fd = conn.fileno()
        buf = self._buffers.get(fd, b"") + chunk
        if b"\n" not in buf:
            self._buffers[fd] = buf
            return
        line, _, rest = buf.partition(b"\n")
        self._buffers[fd] = rest

        try:
            card = Card.from_obj(json.loads(line))
        except (ValueError, TypeError) as exc:
            log.warning("bad request: %s", exc)
            self._forget(conn)
            return

        now = time.monotonic()
        self.queue.append(Pending(card=card, conn=conn, started=now,
                                  deadline=now + card.timeout_ms / 1000))
        log.info("queued %s (%s) from %s", card.id, card.title, card.session_label)
        self._shown = None  # force a redraw

    def _pending_for(self, conn: socket.socket) -> Pending | None:
        for pending in self.queue:
            if pending.conn is conn:
                return pending
        return None

    def _forget(self, conn: socket.socket) -> None:
        try:
            self._buffers.pop(conn.fileno(), None)
        except OSError:
            pass
        try:
            self.sel.unregister(conn)
        except (KeyError, ValueError):
            pass
        try:
            conn.close()
        except OSError:
            pass

    def _respond(self, pending: Pending, choice: str, source: str) -> None:
        if pending.answered:
            return
        pending.answered = True
        answer = Answer(id=pending.card.id, choice=choice, source=source,
                        elapsed_ms=int((time.monotonic() - pending.started) * 1000))
        try:
            pending.conn.sendall(encode(answer))
        except OSError as exc:
            if exc.errno not in (errno.EPIPE, errno.ECONNRESET):
                log.warning("failed to answer %s: %s", pending.card.id, exc)
        self._forget(pending.conn)

    def _drop(self, pending: Pending, source: str, choice: str = "") -> None:
        self._respond(pending, choice, source)
        if pending in self.queue:
            self.queue.remove(pending)
        self._pressed = None
        self._press_origin = None
        self._shown = None
        if source == SRC_TOUCH:
            if choice.startswith("deny"):
                self.stats.denied += 1
            elif choice:
                self.stats.allowed += 1

    def _expire(self) -> None:
        now = time.monotonic()
        for pending in list(self.queue):
            if now >= pending.deadline:
                log.info("card %s timed out -> %r", pending.card.id,
                         pending.card.default)
                self._drop(pending, SRC_TIMEOUT, pending.card.default)

    # -- touch ------------------------------------------------------------
    def _on_touch(self, _fd: int) -> None:
        for event in self.touch.read():
            x = max(0, min(self.fb.width - 1, event.x))
            y = max(0, min(self.fb.height - 1, event.y))
            if not self.queue:
                continue
            if event.kind == "down":
                hit = self._hit(x, y)
                log.debug("down (%d,%d) -> %s", x, y,
                          hit.button_id if hit else "nothing")
                self._pressed = hit.button_id if hit else None
                self._press_origin = (x, y)
                self._shown = None
            elif event.kind == "up":
                hit = self._hit(x, y)
                log.debug("up   (%d,%d) -> %s", x, y,
                          hit.button_id if hit else "nothing")
                if hit and self._pressed == hit.button_id and self._near_origin(x, y):
                    log.info("card %s answered %r", self.queue[0].card.id,
                             hit.button_id)
                    self._drop(self.queue[0], SRC_TOUCH, hit.button_id)
                else:
                    self._pressed = None
                    self._shown = None
                self._press_origin = None

    def _near_origin(self, x: int, y: int) -> bool:
        if self._press_origin is None:
            return True
        ox, oy = self._press_origin
        return abs(x - ox) <= TAP_SLOP and abs(y - oy) <= TAP_SLOP

    def _hit(self, x: int, y: int) -> ui.HitRegion | None:
        for region in self._hits:
            if region.contains(x, y):
                return region
        return None

    # -- rendering --------------------------------------------------------
    def _render(self) -> None:
        if not self.queue:
            state = ("idle", 0, self.idle_status)
            if self._shown == state:
                return
            image = ui.idle_screen(self.fb.size, self.idle_label,
                                   self.idle_status, self.stats.line())
            self._hits = []
            self._blit(image)
            self._shown = state
            return

        pending = self.queue[0]
        remaining = max(0.0, pending.deadline - time.monotonic())
        state = (pending.card.id, int(remaining), self._pressed)
        if self._shown == state:
            return

        image, hits = ui.render(pending.card, self.fb.size,
                                queue=(1, len(self.queue)),
                                remaining=remaining, pressed=self._pressed)
        if [h.button_id for h in hits] != [h.button_id for h in self._hits]:
            log.debug("regions: %s",
                      [(h.button_id, h.x0, h.y0, h.x1, h.y1) for h in hits])
        self._hits = hits
        self._blit(image)
        self._shown = state

    def _blit(self, image: Image.Image) -> None:
        try:
            self.fb.blit(image)
        except OSError as exc:
            log.error("panel write failed (%s); dropping every card", exc)
            for pending in list(self.queue):
                self._drop(pending, "unavailable")
            self._running = False


def main() -> int:
    level = logging.DEBUG if os.environ.get("CCDISP_DEBUG") else logging.INFO
    logging.basicConfig(level=level,
                        format="%(asctime)s %(levelname)s %(message)s")
    try:
        daemon = Daemon()
    except (FbNotFound, TouchNotFound) as exc:
        log.error("%s", exc)
        return 1
    except PermissionError as exc:
        log.error("%s — is the udev rule installed?", exc)
        return 1

    signal.signal(signal.SIGTERM, daemon.stop)
    signal.signal(signal.SIGINT, daemon.stop)
    try:
        daemon.run()
    finally:
        daemon.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
