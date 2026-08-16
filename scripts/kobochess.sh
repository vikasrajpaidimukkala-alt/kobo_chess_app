#!/bin/sh
# Launch Kobo Chess from NickelMenu, then always restore Nickel.
#
# This is the "do not brick the device" path:
#   1. Stop Nickel (and sickel, or Nickel would respawn on top of us).
#   2. Run the game. EXIT / page-turn / SIGTERM all leave the binary.
#   3. The EXIT trap restarts Nickel even if the game crashes.
#   4. If Nickel cannot be restarted, reboot instead of leaving a black UI.
#
# We never touch Wi-Fi or Bluetooth. Reloading MediaTek modules on a
# Libra Colour is a known way to panic the kernel and lose NickelMenu.

export PATH="/sbin:/bin:/usr/sbin:/usr/bin"

APPDIR="/mnt/onboard/.adds/kobochess"
LOG="${APPDIR}/kobochess.log"
VIA_NICKEL=0

cd "${APPDIR}" || exit 1

if pkill -0 nickel 2>/dev/null; then
    VIA_NICKEL=1
fi

log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >>"${LOG}"
}

restore_nickel() {
    if [ "${VIA_NICKEL}" != "1" ]; then
        return 0
    fi

    log "Restoring Nickel"
    if /bin/sh "${APPDIR}/restart-nickel.sh" >>"${LOG}" 2>&1; then
        log "Nickel restart issued"
        return 0
    fi

    log "Nickel restart failed; rebooting so the device is not left dead"
    sync
    reboot
}

trap restore_nickel EXIT INT TERM HUP

{
    echo
    echo "==== $(date) pid $$ ===="
} >>"${LOG}"

if [ "${VIA_NICKEL}" = "1" ]; then
    NICKEL_PID="$(pidof -s nickel 2>/dev/null)"
    if [ -n "${NICKEL_PID}" ] && [ -r "/proc/${NICKEL_PID}/environ" ]; then
        # Same env siphon KOReader uses so nickel can start again.
        # shellcheck disable=SC2046
        export $(grep -s -E -e '^(DBUS_SESSION_BUS_ADDRESS|NICKEL_HOME|WIFI_MODULE|LANG|INTERFACE|PLATFORM|PRODUCT)=' "/proc/${NICKEL_PID}/environ")
    fi

    if [ -z "${PRODUCT}" ] && [ -x /bin/kobo_config.sh ]; then
        PRODUCT="$(/bin/kobo_config.sh 2>/dev/null)"
        export PRODUCT
    fi

    log "Stopping Nickel (product=${PRODUCT} platform=${PLATFORM})"
    sync

    killall -q -TERM nickel hindenburg sickel fickel strickel fontickel adobehost foxitpdf iink dhcpcd-dbus fmon 2>/dev/null

    i=0
    while pkill -0 nickel 2>/dev/null; do
        i=$((i + 1))
        if [ "${i}" -ge 20 ]; then
            log "Nickel still running; sending KILL"
            killall -q -KILL nickel hindenburg sickel 2>/dev/null
            break
        fi
        usleep 250000 2>/dev/null || sleep 1
    done

    rm -f /tmp/nickel-hardware-status

    # Let Nickel's last e-ink update finish so GCC16 does not collide.
    usleep 1000000 2>/dev/null || sleep 1
fi

log "Starting kobochess $(ls -l "${APPDIR}/kobochess" 2>/dev/null | awk '{print $5, $6, $7, $8}')"
"${APPDIR}/kobochess" >>"${LOG}" 2>&1
rc=$?
log "kobochess exited ${rc}"
sync

exit "${rc}"
