#!/bin/sh
export PATH=/sbin:/bin:/usr/sbin:/usr/bin HOME=/root
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mkdir -p /dev/pts /tmp /root
mount -t devpts devpts /dev/pts
mount -t tmpfs -o mode=1777 tmpfs /tmp
exec </dev/console >/dev/console 2>&1
echo 'ReRead installer: starting...'
for driver in ehci_pci ehci_hcd uhci_hcd xhci_pci xhci_hcd usb_storage uas sd_mod ata_piix pata_sch vfat nls_cp437 nls_iso8859_1 nls_utf8 usbhid hid_generic atkbd evdev button fbcon; do
    modprobe "$driver" >>/tmp/drivers.log 2>&1
done
/usr/bin/setfont -C /dev/tty0 /font-large.psf 2>/tmp/font.log || /usr/bin/setfont -C /dev/tty0 /font-tall.psf 2>>/tmp/font.log
printf '\033%%G' > /dev/tty0
exec /bin/sh /install
