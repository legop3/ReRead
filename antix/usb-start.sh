#!/bin/sh
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
mkdir -p /var/log/reader /var/lib/reader/files
exec >>/var/log/reader/usb.log 2>&1
echo "$(date -Iseconds) USB startup"
echo 'USB: starting controller...' >/run/reader-usb-status
modprobe iusbc || { echo 'USB: controller load failed' >/run/reader-usb-status; sleep 30; exit 1; }
set -- /sys/class/udc/*
if [ ! -e "$1" ]; then
    echo 'USB: no device controller found' >/run/reader-usb-status
    sleep 30
    exit 1
fi
modprobe g_ether dev_addr=02:52:44:52:00:01 host_addr=02:52:44:52:00:02 qmult=1 || {
    echo 'USB: Ethernet gadget failed' >/run/reader-usb-status
    sleep 30
    exit 1
}
for attempt in 1 2 3 4 5; do
    [ -e /sys/class/net/usb0 ] && break
    sleep 1
done
ip address replace 192.168.7.1/24 dev usb0 || exit 1
ip link set usb0 up || exit 1
echo 'USB: http://192.168.7.1' >/run/reader-usb-status
exec busybox udhcpd -f /etc/reader/udhcpd.conf
