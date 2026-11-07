#!/bin/sh
export PATH=/sbin:/bin:/usr/sbin:/usr/bin
export HOME=/root
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t tmpfs -o mode=0755 tmpfs /dev
mknod /dev/console c 5 1
mknod /dev/null c 1 3
mknod /dev/tty c 5 0
mknod /dev/tty0 c 4 0
mknod /dev/tty1 c 4 1
mkdir -p /dev/pts /tmp /media /root
mount -t devpts devpts /dev/pts
exec </dev/console >/dev/console 2>&1
echo
echo 'READER WORKBENCH INSTALLER'
echo 'Starting the RAM-based installer. Nothing is installed automatically.'
echo "Kernel: $(uname -r)"
for driver in ehci_pci ehci_hcd uhci_hcd ohci_pci xhci_pci usb_storage uas \
    hid usbhid hid_generic hid_apple sd_mod ahci ata_piix ata_generic pata_sch \
    virtio_pci virtio_blk ext2 vfat nls_cp437 nls_iso8859_1; do
    modprobe "$driver" >> /tmp/drivers.log 2>&1
done
while :; do
    /bin/sh /installer-menu
    echo 'Installer exited. Opening a recovery shell; type exit to return.'
    /bin/sh
done
