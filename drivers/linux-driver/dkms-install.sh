#!/bin/sh
# Stage this driver into /usr/src and register it with DKMS, so it rebuilds
# itself whenever a new kernel is installed.
#
# DKMS copies a single source directory, but the shared protocol header lives
# outside this one (drivers/common/inc/protocol.h). So we assemble a staging
# tree that carries its own copy under common/ — the Makefile looks in both
# places and needs no second variant.
#
# Run with sudo.
set -eu

NAME=rp-usbdisplay
VERSION=1.0.0

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo=$(CDPATH= cd -- "$here/../.." && pwd)
dest=/usr/src/$NAME-$VERSION

command -v dkms >/dev/null || { echo "dkms is not installed" >&2; exit 1; }
[ "$(id -u)" = 0 ] || { echo "run this with sudo" >&2; exit 1; }

echo "staging $dest"
rm -rf "$dest"
mkdir -p "$dest/src/inc" "$dest/common/inc"
cp "$here/Makefile" "$here/dkms.conf" "$dest/"
cp "$here"/src/*.c "$dest/src/"
cp "$here"/src/inc/*.h "$dest/src/inc/"
cp "$repo/drivers/common/inc/protocol.h" "$dest/common/inc/"

# A previous registration would refuse to be replaced, so clear it first.
dkms remove -m "$NAME" -v "$VERSION" --all >/dev/null 2>&1 || true

dkms add -m "$NAME" -v "$VERSION"
dkms build -m "$NAME" -v "$VERSION"
dkms install -m "$NAME" -v "$VERSION" --force

echo
dkms status -m "$NAME"
echo
echo "Reload with: sudo modprobe -r rp_usbdisplay && sudo modprobe rp_usbdisplay"
