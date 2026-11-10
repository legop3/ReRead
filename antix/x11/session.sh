#!/bin/sh
# xinit owns the X server lifetime; Matchbox owns window stacking and focus.
matchbox-window-manager -use_titlebar no -use_cursor no -use_lowlight no >> /var/log/reader/window-manager.log 2>&1 &
wm_pid=$!
trap 'kill "$wm_pid" 2>/dev/null || true' EXIT
/usr/local/libexec/reader-session < /dev/tty1 >> /var/log/reader/ui.log 2>&1
