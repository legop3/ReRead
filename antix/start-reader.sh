#!/bin/sh
# Supervised by antiX runit; normal distro boot, udev and networking stay active.
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
console=/dev/tty1
case " $(cat /proc/cmdline) " in *' console=ttyS0,'*) console=/dev/ttyS0 ;; esac
exec <"$console" >"$console" 2>&1
mkdir -p /var/lib/reader /var/log/reader
modprobe reader_backlight 2>>/var/log/reader/display.log || true
/usr/local/sbin/reader-brightness init >>/var/log/reader/display.log 2>&1 || true
/usr/local/sbin/reader-audio init >>/var/log/reader/audio.log 2>&1 || true
setfont -C /dev/tty1 /usr/share/reader/font-large.psf 2>/var/log/reader/font.log ||
    setfont -C /dev/tty1 /usr/share/reader/font-tall.psf 2>>/var/log/reader/font.log
printf '\033%%G' > /dev/tty1
chvt 1 2>/dev/null || true
if [ ! -e /run/reader-started ]; then
    /usr/local/sbin/reader-fan-reset >>/var/log/reader/fan.log 2>&1 || true
    rm -f /var/lib/reader/reboot-required
    touch /run/reader-started
fi
echo "ReRead base: antiX Core $(uname -r)"
findmnt -n -o TARGET,SOURCE,FSTYPE /
findmnt -n -o TARGET,SOURCE,FSTYPE /boot || true
# The X server uses the existing framebuffer; apps use ordinary X windows.
export REREAD_FILES_DIR=/var/lib/reader/files
export QT_QPA_PLATFORM=xcb
export QT_XCB_GL_INTEGRATION=none
export QT_QPA_GENERIC_PLUGINS=
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/reread}
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
exec xinit /usr/local/libexec/reader-x11-session -- /usr/lib/xorg/Xorg :0 vt1 -keeptty -nolisten tcp -config /usr/share/reader/xorg.conf -logfile /var/log/reader/Xorg.log
