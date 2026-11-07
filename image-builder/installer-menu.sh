#!/bin/sh
. /image-info
SOURCE=

make_nodes() {
    for entry in /sys/block/* /sys/block/*/*; do
        [ -f "$entry/dev" ] || continue
        name=${entry##*/}
        devnum=$(cat "$entry/dev")
        [ -b "/dev/$name" ] || mknod "/dev/$name" b "${devnum%:*}" "${devnum#*:}"
    done
}

disks() {
    make_nodes
    echo 'Disk              MiB    Removable  Model'
    for entry in /sys/block/sd* /sys/block/hd* /sys/block/vd*; do
        [ -f "$entry/size" ] || continue
        sectors=$(cat "$entry/size")
        model=$(cat "$entry/device/model" 2>/dev/null)
        removable=$(cat "$entry/removable")
        printf '/dev/%-10s %-6s %-10s %s\n' "${entry##*/}" "$((sectors / 2048))" "$removable" "$model"
    done
}

find_media() {
    [ -n "$SOURCE" ] && [ -f /media/READER-INSTALLER ] && return 0
    echo 'Looking for installer files...'
    tries=0
    while [ "$tries" -lt 20 ]; do
        make_nodes
        for disk in /sys/block/sd* /sys/block/hd* /sys/block/vd*; do
            [ -d "$disk" ] || continue
            for entry in "$disk"/*; do
                [ -f "$entry/partition" ] || continue
                name=${entry##*/}
                if mount -t vfat -o ro "/dev/$name" /media 2>/dev/null; then
                    if [ -f /media/READER-INSTALLER ] && [ -f /media/payload/reader-workbench.img.gz ]; then
                        SOURCE=${disk##*/}
                        echo "Installer media: /dev/$name"
                        return 0
                    fi
                    umount /media
                fi
            done
        done
        tries=$((tries + 1))
        sleep 1
    done
    echo 'Installer media not detected. The shell remains available.'
    echo 'Use disks, retry, or shell. Driver messages: /tmp/drivers.log'
    return 1
}

verify_payload() {
    find_media || return 1
    echo 'Checking the image on USB before any disk write...'
    actual=$(sha256sum /media/payload/reader-workbench.img.gz)
    actual=${actual%% *}
    if [ "$actual" != "$IMAGE_GZ_SHA256" ]; then
        echo 'ERROR: image checksum mismatch. Nothing was written.'
        return 1
    fi
    gzip -t /media/payload/reader-workbench.img.gz || return 1
    echo 'Image checksum and gzip integrity OK.'
}

target_ok() {
    case "$TARGET" in /dev/sd[a-z]|/dev/hd[a-z]|/dev/vd[a-z]) ;; *) return 1 ;; esac
    name=${TARGET#/dev/}
    [ -b "$TARGET" ] && [ -f "/sys/block/$name/size" ] || return 1
    [ "$name" != "$SOURCE" ] || return 1
    [ "$(cat "/sys/block/$name/removable")" = 0 ] || return 1
    case "$(readlink -f "/sys/block/$name/device")" in *usb*) return 1 ;; esac
    # Explicitly restrict this first build to the Reader's known 4 GB SSD.
    [ "$(cat "/sys/block/$name/size")" = 7821072 ] || return 1
    model=$(cat "/sys/block/$name/device/model" 2>/dev/null | tr -d ' ')
    [ "$model" = SSDPAPS0004G1 ] || return 1
    # The installer never mounts the target, but also reject manual mounts.
    if grep -q "^$TARGET" /proc/mounts; then return 1; fi
    return 0
}

install_image() {
    verify_payload || return 1
    disks
    echo
    echo 'This replaces the Reader internal SSD, including the stock software.'
    echo 'Enter the whole internal disk path shown above (or Enter to cancel):'
    read -r TARGET
    [ -n "$TARGET" ] || return 0
    if ! target_ok; then
        echo 'Refused: target must be the known internal SSD, not USB or a partition.'
        return 1
    fi
    echo "To erase $TARGET and install Reader Workbench, type exactly: INSTALL $TARGET"
    read -r confirmation
    [ "$confirmation" = "INSTALL $TARGET" ] || { echo 'Cancelled.'; return 0; }
    target_ok || { echo 'Target changed or mounted; refusing.'; return 1; }
    echo 'Writing internal SSD. Keep power connected; do not unplug the USB.'
    gzip -dc /media/payload/reader-workbench.img.gz | dd of="$TARGET" bs=1048576
    write_status=$?
    sync
    [ "$write_status" = 0 ] || { echo 'WRITE FAILED. Do not reboot into this disk.'; return 1; }
    echo 'Reading the written image back for verification...'
    actual=$(dd if="$TARGET" bs=1048576 count="$IMAGE_MIB" 2>/tmp/readback.log | sha256sum)
    actual=${actual%% *}
    if [ "$actual" != "$IMAGE_SHA256" ]; then
        echo 'READBACK FAILED. Installation is incomplete; do not boot the disk.'
        return 1
    fi
    echo 'INSTALLATION VERIFIED. Type poweroff, remove the USB, then turn on.'
    echo 'Desktop user/password: reader / reader. Change the password with passwd.'
}

find_media
while :; do
    echo
    echo 'Commands: install | verify | disks | retry | shell | reboot | poweroff'
    printf 'Reader installer> '
    read -r action || { /bin/sh; continue; }
    case "$action" in
        install) install_image ;;
        verify) verify_payload ;;
        disks) disks ;;
        retry) find_media ;;
        shell) echo 'Type exit to return.'; /bin/sh ;;
        reboot) sync; reboot -f ;;
        poweroff) sync; poweroff -f ;;
        '') ;;
        *) echo 'Unknown command.' ;;
    esac
done
