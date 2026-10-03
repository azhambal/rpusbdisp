#!/bin/sh
# Install the udev rule that gives a desktop user access to the RoboPeak panel
# and its touchscreen.  Run with sudo; pass the target user as $1 (defaults to
# the user invoking sudo).
set -eu

user="${1:-${SUDO_USER:-$(id -un)}}"
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
dest=/etc/udev/rules.d/99-rpusbdisp-uaccess.rules

id -- "$user" >/dev/null || exit 1

sed "s/@USER@/$user/g" "$here/udev/99-rpusbdisp-uaccess.rules.in" >"$dest"
chmod 0644 "$dest"

# Stop X11 from adopting the touchscreen as a second mouse.  The udev rule above
# handles hotplug; this covers a device the running X server already grabbed.
xorg_dir=/etc/X11/xorg.conf.d
if [ -d "$xorg_dir" ] || mkdir -p "$xorg_dir" 2>/dev/null; then
    install -m 0644 "$here/udev/99-rpusbdisp-xorg-ignore.conf" \
        "$xorg_dir/99-rpusbdisp-xorg-ignore.conf"
    echo "installed $xorg_dir/99-rpusbdisp-xorg-ignore.conf"
fi

udevadm control --reload
udevadm trigger --subsystem-match=graphics --subsystem-match=input

echo "installed $dest for user $user"
ls -l /dev/fb1 2>/dev/null || true
echo
echo "Replug the panel so X drops the touchscreen it already grabbed."
