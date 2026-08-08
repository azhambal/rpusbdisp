#!/bin/bash
#
#    RoboPeak USB LCD Display Linux Driver
#
#    Installs the driver through DKMS so it is rebuilt automatically
#    whenever a new kernel is installed.
#
#    Usage:  sudo ./dkms-install.sh          install / upgrade
#            sudo ./dkms-install.sh remove   uninstall
#
set -eu

NAME=rp-usbdisplay
VER=1.0.0
DEST=/usr/src/${NAME}-${VER}
HERE=$(cd "$(dirname "$0")" && pwd)

if [ "$(id -u)" -ne 0 ]; then
    echo "нужны права root: sudo $0 $*" >&2
    exit 1
fi

if ! command -v dkms >/dev/null; then
    echo "dkms не установлен. Поставь его: apt install dkms" >&2
    exit 1
fi

remove_existing() {
    if dkms status -m "$NAME" 2>/dev/null | grep -q "$NAME"; then
        echo "удаляю предыдущую версию из dkms..."
        dkms remove -m "$NAME" -v "$VER" --all || true
    fi
    rm -rf "$DEST"
}

if [ "${1:-}" = "remove" ]; then
    remove_existing
    echo "удалено."
    exit 0
fi

remove_existing

echo "раскладываю исходники в $DEST ..."
mkdir -p "$DEST/src/inc" "$DEST/common/inc"
cp "$HERE"/src/*.c            "$DEST/src/"
cp "$HERE"/src/inc/*.h        "$DEST/src/inc/"
cp "$HERE"/../common/inc/protocol.h "$DEST/common/inc/"
cp "$HERE"/dkms.conf          "$DEST/"
cp "$HERE"/Makefile.dkms      "$DEST/Makefile"

echo "регистрирую в dkms ..."
dkms add     -m "$NAME" -v "$VER"
dkms build   -m "$NAME" -v "$VER"
dkms install -m "$NAME" -v "$VER" --force

echo
dkms status -m "$NAME"
echo
echo "готово. Модуль будет пересобираться автоматически при обновлении ядра."
