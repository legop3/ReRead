#!/bin/sh
export PATH=/sbin:/bin:/usr/sbin:/usr/bin HOME=/root
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t tmpfs -o mode=0755 tmpfs /dev
mknod /dev/console c 5 1
mknod /dev/null c 1 3
mknod /dev/tty0 c 4 0
mknod /dev/tty1 c 4 1
mkdir -p /dev/input /dev/pts /tmp /root
mount -t devpts devpts /dev/pts
exec </dev/console >/dev/console 2>&1
echo 'Reader POC: starting...'
for driver in ehci_hcd uhci_hcd usbhid hid evdev sd_mod ata_piix pata_sch fbcon vesafb; do
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
while :; do
    /buttons
    echo 'Button viewer stopped. Restarting...'
    sleep 1
done
