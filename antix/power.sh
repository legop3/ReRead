#!/bin/sh
# Called by the Reader UI. Suspend returns here after wake; shutdown uses init.
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
mkdir -p /var/log/reader
exec 2>>/var/log/reader/power.log
case "$1" in
    suspend)
        if ! grep -qw mem /sys/power/state; then
            echo 'Suspend-to-RAM is unavailable.' >&2
            exit 1
        fi
        # Select S3 when firmware offers it, otherwise use the kernel's mem default.
        if [ -r /sys/power/mem_sleep ] && grep -qw deep /sys/power/mem_sleep; then
            echo deep > /sys/power/mem_sleep || exit 1
        fi
        echo "$(date -Iseconds) suspend" >> /var/log/reader/power.log
        sync
        echo mem > /sys/power/state || exit 1
        echo "$(date -Iseconds) resumed" >> /var/log/reader/power.log
        /usr/local/sbin/reader-fan-reset >>/var/log/reader/fan.log 2>&1 || true
        /usr/local/sbin/reader-audio init >>/var/log/reader/audio.log 2>&1 || true
        setfont -C /dev/tty1 /usr/share/reader/font-large.psf 2>/dev/null ||
            setfont -C /dev/tty1 /usr/share/reader/font-tall.psf 2>/dev/null || true
        ;;
    *) exit 2 ;;
esac
