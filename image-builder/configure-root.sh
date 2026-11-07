#!/bin/sh
set -eu
ROOT=/work/build/debian-root
cat > "$ROOT/etc/apt/sources.list" <<'EOF'
deb https://deb.debian.org/debian bookworm main contrib non-free-firmware
deb https://deb.debian.org/debian bookworm-updates main contrib non-free-firmware
deb https://deb.debian.org/debian-security bookworm-security main contrib non-free-firmware
EOF
printf '#!/bin/sh\nexit 101\n' > "$ROOT/usr/sbin/policy-rc.d"
chmod +x "$ROOT/usr/sbin/policy-rc.d"
chroot "$ROOT" apt-get update
chroot "$ROOT" env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    linux-image-686-pae initramfs-tools kmod grub-pc-bin grub2-common busybox-static \
    systemd-sysv systemd-timesyncd udev dbus dbus-x11 sudo \
    xserver-xorg-core xserver-xorg-legacy xserver-xorg-input-libinput xserver-xorg-video-fbdev \
    xserver-xorg-video-vesa xinit xauth x11-xserver-utils x11-utils \
    fluxbox tint2 xterm pcmanfm mousepad netsurf-gtk fonts-dejavu-core \
    alsa-utils connman wpasupplicant wireless-regdb firmware-linux-free \
    firmware-realtek firmware-atheros pciutils usbutils iproute2 procps \
    ca-certificates curl less
rm "$ROOT/usr/sbin/policy-rc.d"
printf 'reader\n' > "$ROOT/etc/hostname"
printf '127.0.0.1 localhost\n127.0.1.1 reader\n' > "$ROOT/etc/hosts"
printf 'UUID=3b1293e9-5462-4b91-b10e-d9fd5e0f2616 / ext4 defaults,noatime,errors=remount-ro 0 1\n' > "$ROOT/etc/fstab"
chroot "$ROOT" id -u reader >/dev/null 2>&1 || chroot "$ROOT" useradd -m -s /bin/bash -G sudo,audio,video,input,plugdev reader
printf 'reader:reader\n' | chroot "$ROOT" chpasswd
mkdir -p "$ROOT/etc/systemd/system/getty@tty1.service.d"
cat > "$ROOT/etc/systemd/system/getty@tty1.service.d/autologin.conf" <<'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin reader --noclear %I $TERM
EOF
mkdir -p "$ROOT/etc/connman" "$ROOT/etc/systemd/journald.conf.d"
printf 'allowed_users=console\nneeds_root_rights=yes\n' > "$ROOT/etc/X11/Xwrapper.config"
printf '[Journal]\nStorage=volatile\nRuntimeMaxUse=16M\n' > "$ROOT/etc/systemd/journald.conf.d/reader.conf"
printf '[General]\nPreferredTechnologies=ethernet,wifi\n' > "$ROOT/etc/connman/main.conf"
mkdir -p "$ROOT/etc/initramfs-tools/conf.d"
printf 'MODULES=most\nCOMPRESS=gzip\n' > "$ROOT/etc/initramfs-tools/conf.d/reader"
cat >> "$ROOT/etc/initramfs-tools/modules" <<'EOF'
# Reader's internal SSD uses Poulsbo PATA, not a USB drive.
pata_sch
ata_piix
ehci_pci
uhci_hcd
usb_storage
hid_apple
EOF
HOME_DIR="$ROOT/home/reader"
mkdir -p "$HOME_DIR/.fluxbox" "$HOME_DIR/Desktop"
cat > "$HOME_DIR/.profile" <<'EOF'
if [ -z "${DISPLAY:-}" ] && [ "$(tty)" = /dev/tty1 ]; then
    case "$(cat /proc/cmdline)" in
      *reader.text=1*) echo 'Text mode. Type startx to try the desktop.' ;;
      *) startx; echo 'Desktop stopped. Type startx to retry, or use this shell.' ;;
    esac
fi
EOF
cat > "$HOME_DIR/.xinitrc" <<'EOF'
#!/bin/sh
export GDK_SCALE=2
export GDK_DPI_SCALE=0.5
xrdb -merge "$HOME/.Xresources"
xsetroot -solid '#18252f'
tint2 &
exec fluxbox
EOF
cat > "$HOME_DIR/.Xresources" <<'EOF'
Xft.dpi: 192
XTerm*faceName: DejaVu Sans Mono
XTerm*faceSize: 14
XTerm*background: #101820
XTerm*foreground: #f0f0f0
XTerm*saveLines: 1000
EOF
chmod +x "$HOME_DIR/.xinitrc"
cat > "$HOME_DIR/.gtkrc-2.0" <<'EOF'
gtk-font-name="DejaVu Sans 18"
gtk-icon-sizes="gtk-menu=32,32:gtk-button=32,32:gtk-large-toolbar=48,48"
EOF
cat > "$HOME_DIR/.fluxbox/init" <<'EOF'
session.screen0.toolbar.visible: false
session.screen0.menuMode: Click
session.screen0.defaultDeco: NORMAL
session.menuFile: ~/.fluxbox/menu
session.keyFile: ~/.fluxbox/keys
session.styleOverlay: ~/.fluxbox/overlay
EOF
printf 'menu.frame.font: DejaVu Sans-20\nmenu.title.font: DejaVu Sans-20:bold\nwindow.font: DejaVu Sans-18\n' > "$HOME_DIR/.fluxbox/overlay"
cat > "$HOME_DIR/.fluxbox/menu" <<'EOF'
[begin] (Reader)
 [exec] (Terminal) {xterm}
 [exec] (Files) {pcmanfm}
 [exec] (Text editor) {mousepad}
 [exec] (Web browser) {netsurf-gtk}
 [exec] (Sound mixer) {xterm -e alsamixer}
 [exec] (Network setup) {xterm -e sudo connmanctl}
 [exec] (Hardware details) {xterm -hold -e sh -c 'lspci -nnk; lsusb; free -m'}
 [exec] (Read me) {mousepad ~/Desktop/START-HERE.txt}
 [reconfig] (Reload interface)
 [exit] (Exit desktop)
 [exec] (Shut down) {systemctl poweroff}
[end]
EOF
cat > "$HOME_DIR/.fluxbox/keys" <<'EOF'
OnDesktop Mouse3 :RootMenu
Mod4 space :RootMenu
Mod4 Return :Exec xterm
Mod1 F2 :Exec fbrun
Mod4 e :Exec pcmanfm
Mod4 m :Exec mousepad
Mod4 b :Exec netsurf-gtk
Mod1 Tab :NextWindow {groups}
Mod1 F4 :Close
Control Mod1 Right :NextWorkspace
Control Mod1 Left :PrevWorkspace
EOF
cat > "$HOME_DIR/Desktop/START-HERE.txt" <<'EOF'
READER WORKBENCH — FIRST HARDWARE TEST BUILD

This is a general-purpose Debian 12 i386 desktop, not the original Reader app.
User: reader     Initial password: reader
Change it with passwd. Root login is locked. sudo uses the reader password.

Windows+Space: application menu (right click also works).
Windows+Enter: terminal. Alt+F2: run any command.
Windows+E: files. Windows+M: editor. Windows+B: browser.
Alt+Tab: switch windows. Alt+F4: close window.
Ctrl+Alt+F2: text console. Ctrl+Alt+F1: return.

Network: open Network setup, then use connmanctl.
For Wi-Fi: enable wifi, agent on, scan wifi, services, connect <service>.
USB Wi-Fi compatibility depends on its chipset. Wired DHCP is automatic.
Browser is lightweight NetSurf; many modern JavaScript-heavy sites won't work.
Use apt to install other applications once networking works.

Display scaling is provisional. No guessed LCD modeline is forced.
If graphics fail, choose Text console in GRUB. startx tries graphics again.
Camera/flash, physical-button mapping, mini-USB gadget support, cooling and
power behavior are not yet validated under this kernel. Keep initial tests
short and check temperature/fan behavior before sustained workloads.
Stock firmware/libraries remain available in the project evidence on your PC.
EOF
chroot "$ROOT" chown -R reader:reader /home/reader
chroot "$ROOT" update-initramfs -u -k all
chroot "$ROOT" apt-get clean
rm -rf "$ROOT/var/lib/apt/lists/"* "$ROOT/tmp/"*
printf 'nameserver 1.1.1.1\n' > "$ROOT/etc/resolv.conf"
chroot "$ROOT" dpkg-query -W > /work/out/packages.tsv
touch "$ROOT/.reader-configured"
