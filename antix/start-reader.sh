#!/bin/sh
# Supervised by antiX runit; normal distro boot, udev and networking stay active.
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
console=/dev/tty1
case " $(cat /proc/cmdline) " in *' console=ttyS0,'*) console=/dev/ttyS0 ;; esac
exec <"$console" >"$console" 2>&1
mkdir -p /var/lib/reader /var/log/reader
ln -sfn /var/log/reader/buttons.log /tmp/buttons.log
setfont -C /dev/tty1 /usr/share/reader/font-large.psf 2>/var/log/reader/font.log ||
    setfont -C /dev/tty1 /usr/share/reader/font-tall.psf 2>>/var/log/reader/font.log
printf '\033%%G' > /dev/tty1
chvt 1 2>/dev/null || true
if [ ! -e /run/reader-started ]; then
    rm -f /var/lib/reader/reboot-required
    count=$(cat /var/lib/reader/boot-count 2>/dev/null)
    case "$count" in ''|*[!0-9]*) count=0 ;; esac
    count=$((count + 1))
    echo "$count" > /var/lib/reader/boot-count
    echo "BOOT $count antiX Core kernel=$(uname -r) root=$(findmnt -n -o SOURCE /)" >> /var/log/reader/boot.log
    {
        cat /proc/bus/input/devices
        cat /proc/fb
        dmesg
    } >> /var/log/reader/input-devices.log
    touch /run/reader-started
    echo "PERSISTENT BOOT COUNT: $count"
fi
echo "READER BASE: antiX Core $(uname -r)"
findmnt -n -o SOURCE,FSTYPE,OPTIONS /
exec /usr/local/bin/reader-buttons
