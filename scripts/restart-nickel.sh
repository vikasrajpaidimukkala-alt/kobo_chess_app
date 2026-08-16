#!/bin/sh
# Restart Nickel after the chess app exits.
# Mirrors the KOReader nickel.sh idea, without touching Wi-Fi or Bluetooth
# (MediaTek Libra Colour can kernel-panic if those modules are reloaded).

PATH="/sbin:/bin:/usr/sbin:/usr/bin:/usr/lib:"

cd /
unset OLDPWD
unset LC_ALL

export LD_LIBRARY_PATH="/usr/local/Kobo"
export QT_GSTREAMER_PLAYBIN_AUDIOSINK=alsasink
export QT_GSTREAMER_PLAYBIN_AUDIOSINK_DEVICE_PARAMETER=bluealsa:DEV=00:00:00:00:00:00

if [ -e "/etc/init.d/on-animator.sh" ]; then
    /etc/init.d/on-animator.sh &
fi

if [ -e "/dev/mmcblk1p1" ]; then
    umount /mnt/sd 2>/dev/null
fi

sync

if [ -e "/etc/init.d/z-nickel-hardware-status" ]; then
    unset LD_LIBRARY_PATH
    /etc/init.d/z-nickel-hardware-status
    sync
    /etc/rc.local
    exit 0
fi

rm -f /tmp/nickel-hardware-status
mkfifo /tmp/nickel-hardware-status

/usr/local/Kobo/hindenburg &
LIBC_FATAL_STDERR_=1 /usr/local/Kobo/nickel -platform kobo -skipFontLoad &

if [ "${PLATFORM}" != "freescale" ]; then
    udevadm trigger &
fi

exit 0
