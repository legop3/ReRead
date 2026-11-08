#!/bin/sh
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
mkdir -p /var/log/reader /run/wpa_supplicant
exec >>/var/log/reader/wifi.log 2>&1
while :; do
    for device in /sys/class/net/*; do
        if [ -d "$device/wireless" ] || [ -e "$device/phy80211" ]; then
            interface=${device##*/}
            rfkill unblock wifi 2>/dev/null || true
            ip link set "$interface" up || exit 1
            exec wpa_supplicant -i "$interface" -D nl80211,wext -c /etc/wpa_supplicant/reader.conf
        fi
    done
    sleep 2
done
