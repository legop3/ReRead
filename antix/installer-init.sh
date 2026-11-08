#!/bin/sh
export PATH=/sbin:/bin:/usr/sbin:/usr/bin HOME=/root
grep -q ' /proc proc ' /proc/mounts 2>/dev/null || mount -t proc proc /proc
grep -q ' /sys sysfs ' /proc/mounts || mount -t sysfs sysfs /sys
grep -q ' /dev tmpfs ' /proc/mounts || mount -t tmpfs -o mode=0755 tmpfs /dev
for node in console null tty0 tty1; do
    case "$node" in console) major=5; minor=1 ;; null) major=1; minor=3 ;; tty0) major=4; minor=0 ;; tty1) major=4; minor=1 ;; esac
    [ -c "/dev/$node" ] || mknod "/dev/$node" c "$major" "$minor"
done
mkdir -p /dev/input /dev/pts /tmp /root
mount -t devpts devpts /dev/pts
mount -t tmpfs -o mode=1777 tmpfs /tmp
exec </dev/console >/dev/console 2>&1
echo 'Reader installer: starting...'
for driver in ehci_hcd uhci_hcd usbhid hid evdev sd_mod button fbcon vesafb; do
    modprobe "$driver" >> /tmp/drivers.log 2>&1
done
if [ -f /install ]; then
    for driver in usb_storage vfat nls_cp437 nls_iso8859_1; do
        modprobe "$driver" >> /tmp/drivers.log 2>&1
    done
fi
# Make event nodes directly, without starting the stock application or udev rules.
for attempt in 1 2 3 4 5; do
    for entry in /sys/class/input/event*; do
        [ -f "$entry/dev" ] || continue
        name=${entry##*/}; devnum=$(cat "$entry/dev")
        [ -c "/dev/input/$name" ] || mknod "/dev/input/$name" c "${devnum%:*}" "${devnum#*:}"
    done
    sleep 1
done
setfont -C /dev/tty0 /font-large.psf 2>/tmp/font.log || setfont -C /dev/tty0 /font-tall.psf 2>>/tmp/font.log
printf '\033%%G' > /dev/tty0
if [ -f /install ]; then
    /bin/sh /install
    while :; do
        echo 'Installer stopped. Diagnostic shell only; installation will not restart automatically.'
        /bin/sh
        sleep 1
    done
fi
