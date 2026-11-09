#!/bin/sh
# Reader-specific hardware preparation/restoration in elogind's sleep lifecycle.
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
mkdir -p /var/log/reader
exec 2>>/var/log/reader/power.log
case "$1" in
    pre)
        echo "$(date -Iseconds) prepare $2" >>/var/log/reader/power.log
        if [ -S /run/reader-camera.sock ]; then
            if ! reader-camera suspend >>/var/log/reader/camera.log 2>&1; then
                reader-camera resume >>/var/log/reader/camera.log 2>&1 || true
                echo 'ERROR: Reader camera could not prepare for sleep'
                exit 1
            fi
        fi
        sync
        ;;
    post)
        echo "$(date -Iseconds) resumed $2" >>/var/log/reader/power.log
        reader-fan-reset >>/var/log/reader/fan.log 2>&1 || true
        reader-audio init >>/var/log/reader/audio.log 2>&1 || true
        reader-camera resume >>/var/log/reader/camera.log 2>&1 || true
        reader-brightness init >>/var/log/reader/display.log 2>&1 || true
        ;;
esac
