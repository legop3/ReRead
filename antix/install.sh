#!/bin/sh
. /image-info
export PATH=/sbin:/bin:/usr/sbin:/usr/bin

nodes() {
    for entry in /sys/block/* /sys/block/*/*; do
        [ -f "$entry/dev" ] || continue
        name=${entry##*/}; devnum=$(cat "$entry/dev")
        [ -b "/dev/$name" ] || mknod "/dev/$name" b "${devnum%:*}" "${devnum#*:}"
    done
}
disks() {
    nodes
    for entry in /sys/block/sd* /sys/block/hd* /sys/block/vd*; do
        [ -f "$entry/size" ] || continue
        printf '/dev/%s  %s MiB  %s\n' "${entry##*/}" "$(( $(cat "$entry/size") / 2048 ))" "$(cat "$entry/device/model" 2>/dev/null)"
    done
}
target_ok() {
    case "$TARGET" in /dev/sd[a-z]|/dev/hd[a-z]|/dev/vd[a-z]) ;; *) return 1 ;; esac
    name=${TARGET#/dev/}
    [ -b "$TARGET" ] && [ -f "/sys/block/$name/size" ] || return 1
    [ "$(cat "/sys/block/$name/removable")" = 0 ] || return 1
    case "$(readlink -f "/sys/block/$name/device")" in *usb*) return 1 ;; esac
    [ "$(cat "/sys/block/$name/size")" = 7821072 ] || return 1
    [ "$(cat "/sys/block/$name/device/model" | tr -d ' ')" = SSDPAPS0004G1 ] || return 1
    if grep -q "^$TARGET" /proc/mounts; then return 1; fi
}
find_payload() {
    mkdir -p /media/installer
    echo 'Looking for the matching image on USB storage...'
    attempt=0
    while [ "$attempt" -lt 30 ]; do
        nodes
        for disk in /sys/block/sd*; do
            [ -f "$disk/dev" ] || continue
            case "$(readlink -f "$disk/device")" in *usb*) ;; *) continue ;; esac
            for entry in "$disk"/"${disk##*/}"*; do
                [ -f "$entry/dev" ] || continue
                source=/dev/${entry##*/}
                mount -t vfat -o ro "$source" /media/installer 2>>/tmp/storage.log || continue
                marker=
                if [ -f /media/installer/reread/payload.id ]; then
                    read -r marker < /media/installer/reread/payload.id
                fi
                if [ "$marker" = "$BUILD_ID" ] && [ -f /media/installer/reread/reread.img.gz ]; then
                    PAYLOAD=/media/installer/reread/reread.img.gz
                    echo "Image found on $source (mounted read-only)."
                    return 0
                fi
                umount /media/installer || return 1
            done
        done
        attempt=$((attempt + 1))
        sleep 1
    done
    echo 'FAILED: matching USB payload not found.'
    return 1
}
install_image() {
    find_payload || return 1
    # Select by identity, never by the order in which Linux found the drives.
    matches=0; chosen=
    for entry in /sys/block/*; do
        TARGET=/dev/${entry##*/}
        if target_ok; then matches=$((matches + 1)); chosen=$TARGET; fi
    done
    [ "$matches" = 1 ] || { echo "REFUSED: expected one known internal Reader SSD; found $matches."; disks; return 1; }
    TARGET=$chosen
    target_ok || return 1
    echo "Automatically installing to $TARGET (SSDPAPS0004G1)."
    echo 'Keep USB connected until INSTALLATION COMPLETE.'
    echo 'Writing from USB storage...'
    # Stream once. Check both processes without a separate checksum/readback pass.
    mknod /tmp/reader-image.pipe p || return 1
    gzip -dc "$PAYLOAD" > /tmp/reader-image.pipe &
    inflater=$!
    dd if=/tmp/reader-image.pipe of="$TARGET" bs=1048576
    write_status=$?
    wait "$inflater"
    inflate_status=$?
    rm -f /tmp/reader-image.pipe
    sync
    [ "$write_status" = 0 ] && [ "$inflate_status" = 0 ] || { echo 'WRITE FAILED'; return 1; }
    umount /media/installer || return 1
    echo 'INSTALLATION COMPLETE'
    echo 'Rebooting in 5 seconds. USB may now be removed.'
    sleep 5
    reboot -f
    echo 'Automatic reboot failed. Restart the Reader manually.'
    return 1
}

printf '\033[2J\033[HReRead AUTOMATIC INSTALLER\n'
echo 'Installs from USB storage to the known internal Reader SSD.'
install_image
# Failures stop here; never retry a disk write without an explicit new command.
echo 'INSTALLER STOPPED. No automatic retry.'
echo 'Diagnostic shell: /tmp/drivers.log, /tmp/storage.log; type /bin/sh /install to retry.'
exec /bin/sh
