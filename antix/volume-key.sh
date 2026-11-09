#!/bin/sh
# Coalesce keyboard repeats while the existing ALSA helper saves mixer state.
case "$1" in up|down) ;; *) exit 2 ;; esac
exec /usr/bin/flock -n /run/reader-volume-key.lock /bin/sh -c '
    /usr/local/sbin/reader-audio "$1"
    sleep .18
' sh "$1"
