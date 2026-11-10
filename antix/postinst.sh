#!/bin/sh
set -e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
case "$1" in configure) ;; *) exit 0 ;; esac
mkdir -p /var/lib/reader/files /var/lib/reader/apps /var/lib/reader/updates /var/log/reader
# Wi-Fi credentials, hostname, passwords, SSH keys and user data are never reset.
if ! grep -q '^denyinterfaces usb0$' /etc/dhcpcd.conf; then
    printf '\n# ReRead USB serves DHCP with a fixed address.\ndenyinterfaces usb0\n' >>/etc/dhcpcd.conf
fi
chmod 600 /etc/wpa_supplicant/reader.conf
rm -f /etc/runit/runsvdir/default/getty-tty1 /etc/runit/runsvdir/default/getty-ttyS0
kernel=$(cat /usr/share/reader/kernel-version)
depmod "$kernel"
# The package dependency pins the tested kernel/driver pair.
if [ -r /sys/module/iusbc/srcversion ]; then
    installed=$(modinfo -F srcversion "/lib/modules/$kernel/extra/iusbc.ko")
    if [ "$(cat /sys/module/iusbc/srcversion)" != "$installed" ]; then
        echo 'USB controller driver changed. Reboot to load it.' >/var/lib/reader/reboot-required
    fi
fi
# Never reload USB networking during an update over that connection.
if [ -S /run/reader-camera.sock ]; then
    /usr/local/sbin/reader-camera suspend || true
fi
# Management daemons now share the USB-session lifetime; retire old supervisors.
for service in reader-web reader-ssh; do
    if [ -L "/etc/runit/runsvdir/default/$service" ] && [ -p "/etc/sv/$service/supervise/control" ]; then
        sv -w 5 down "/etc/sv/$service" || true
    fi
    rm -f "/etc/runit/runsvdir/default/$service"
done
ldconfig
/usr/local/sbin/reader-upower-configure
live=no
[ ! -p /etc/sv/reader/supervise/control ] || live=yes
# Use one runit supervisor per daemon, retiring instances started by distro
# maintainer scripts. These services do not need a desktop session.
for service in elogind reader-hotkeys cron; do
    link=/etc/runit/runsvdir/default/$service
    if [ ! -L "$link" ]; then
        distro=$service
        [ "$service" != reader-hotkeys ] || distro=triggerhappy
        if [ -x "/etc/init.d/$distro" ]; then
            if [ "$live" = yes ]; then "/etc/init.d/$distro" stop || true; fi
            update-rc.d "$distro" disable || true
        fi
        ln -s "/etc/sv/$service" "$link"
    fi
done
# Stop the display before changing the live keyboard transport assignments.
# evdev/Qt window ownership is supplied by the new core; existing app IDs stay stable.
if [ "$live" = yes ]; then sv -w 15 down reader || true; fi
udevadm hwdb --update
if [ "$live" = yes ]; then
    udevadm trigger --subsystem-match=input --action=change
    udevadm settle
fi
for service in reader-hotkeys reader reader-battery reader-camera; do
    if [ -p "/etc/sv/$service/supervise/control" ]; then
        restart_timeout=5
        [ "$service" != reader-camera ] || restart_timeout=45
        sv -w "$restart_timeout" restart "$service" || true
    fi
done
if [ -n "${2:-}" ]; then
    echo 'ReRead was updated. Reboot to apply all background-service changes.' >/var/lib/reader/reboot-required
fi
exit 0
