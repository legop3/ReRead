# ReRead build and system notes

ReRead is a handheld platform built on **antiX 26.1 Core i386**, kernel `5.10.256-antix.1-486-smp`. It uses antiX's package management and normal runit startup, with a small button interface and no desktop. There is one maintained OS image.

## Releases and updates

The full installer installs antiX plus the **same `reread` Debian package used
for later updates**. The source of truth is this repository, not a running
Reader. Version is set in [VERSION](VERSION); [release.py](release.py) builds
the platform package from source. Personal files, network credentials, host
identity and installed apps are not part of its payload.

For ordinary updates, open **Updates** in the USB web interface and select
`reread_<version>_i386.deb`. The update installs in place without formatting or
replacing the root filesystem. Wi-Fi settings, uploaded files, app data,
hostname, passwords, SSH host keys and authorized keys remain in place.
The worker runs independently of the HTTP server, with persistent status/logs
so the page can reconnect after the server restarts. A reboot notice covers
background-service/driver changes; reboot with `reboot` over SSH when indicated. CLI: `reader-update install /path/to/release.deb`.

**Apps** installs/updates `reread-app-*` packages, lists installed apps, and removes
them while retaining their data and conffiles. Service apps can be enabled or
disabled persistently. There are no bundled apps yet; see the
[app packaging contract](apps/README.md).

Core code/assets, hardware module and service definitions are package-owned.
User data stays in `/var/lib/reader`, app data in `/var/lib/reader/apps/<id>`,
Wi-Fi credentials in `/etc/wpa_supplicant/reader.conf`, and SSH credentials in
their normal system locations. Editable ReRead defaults use dpkg conffiles;
updates use `--force-confold`/`--force-confdef`, without a config-replacement
prompt. Installs will not remove unrelated packages to satisfy dependencies.
App removal uses non-purging dpkg removal and refuses broken dependencies.

The kernel package is pinned by an exact dependency to the hardware-tested
kernel/driver pair. This initial update format updates the ReRead platform and
apps, not the FAT bootloader or a different kernel ABI. Those require a future
tested boot-update release. The antiX userspace remains apt-managed. There is
no hosted release feed/app store yet: upload locally built release packages.

## Installation

```sh
sudo python3 antix/write-usb.py
```

The flasher accepts the original Flash Memory stick and USB DISK 3.0 serial `0719079F1B2AD607`; connect exactly one of them.

Boot the USB through the powered hub. The installer finds the known internal SSD, streams the image from USB, flushes writes and reboots after five seconds. No prompts, full-image checksum scans, decompression pre-tests or readback passes. Decompression and write failures still stop installation. Keep USB attached until **INSTALLATION COMPLETE**.

The SSD image has a 63 MiB FAT boot partition and a 3 GiB ext3 root, after a 1 MiB alignment gap. It fits the Reader's 4 GB SSD. Applications, packages and persistent logs live on SSD; the OS is not loaded into a RAM root. Reinstallation replaces the system and its logs.

## Build

The build container needs the packages in [the Dockerfile](../image-builder/Dockerfile), a `/work` workspace mount, the verified `downloads/antiX-26.1_386-core.iso`, and the extracted stock installer inputs described in [evidence/README.md](../evidence/README.md).

Create the builder once, from the repository root (skip this if the container
already exists):

```sh
docker build -t reread-builder image-builder
docker run -d --name intelreader-validator -v "$PWD:/work" reread-builder sleep infinity
```

On a host with SELinux enforcing, use `-v "$PWD:/work:Z"` for the workspace mount.
Optional VM acceleration needs `--device /dev/kvm` when creating the container;
without it QEMU uses software emulation. No host disks are needed by the builder.

Build the installer, SSD image and platform package:

```sh
docker exec intelreader-validator python3 /work/antix/build.py
```

Outputs in `out/antix/`:

- `reread.img`: full SSD image.
- `reread.img.gz`: compressed SSD payload.
- `reread-installer-usb.img`: bootable automatic installer.
- `reread_<version>_i386.deb`: platform update package.
- `manifest.json`: paths used by the USB writer.

For later platform changes, bump `antix/VERSION` and rebuild just the package.
This uses the matching kernel headers from `build/antix/root`, populated by the
initial full build; it does not rebuild an SSD image or touch a device:

```sh
docker exec intelreader-validator python3 /work/antix/release.py
```

Upload the resulting `.deb` through **Updates**. This is the normal iteration
path once ReRead is installed. Rebuild the full image when preparing a fresh
installer with the latest release. To update just the USB installer around an
already built system:

```sh
docker exec intelreader-validator python3 /work/antix/package_installer.py
```

`docker exec intelreader-validator python3 /work/antix/test-packages.py` runs
platform-upgrade and app-management checks in a disposable chroot, including
settings/data preservation and a web-server restart during installation. It
uses synthetic data and never connects to a Reader.

An optional VM boot/input and two-cycle suspend/resume check is `python3 /work/antix/test.py` inside the container. SSD installation, display, all 19 controls, and Power-button sleep have been confirmed on the Reader.

## Hardware and maintenance

[reader-keys.h](reader-keys.h) defines the measured controls and stable `READER_*` IDs. Hold value 1 means unlocked; value 0 means locked. The viewer displays it without enforcing a lock. Its initial state at boot remains unchecked.

The custom console font is included in early boot and runtime, with a tall-font fallback. Camera, flash and audio integration remain separate work.

Boot counters and logs persist under `/var/lib/reader` and `/var/log/reader`. The installed image locks the ISO's shared root/demo passwords; normal operation needs no login. For maintenance, use a live USB to mount/chroot the SSD, or add `rw init=/bin/sh` to the installed boot command to set local credentials.

The small USB installer uses the proven stock kernel solely to write the SSD; it is not a second OS build. The installed system uses antiX's kernel and userspace. The BIOS loads a kernel/initramfs copy from FAT, so kernel updates currently require rebuilding the image; ordinary apt packages live directly on the root filesystem.

## Current keyboard compatibility test

On the antiX kernel, the owner reports missing/intermittent controls, unwanted View Toggle press/release events (`227`, scan `0x6b`), and repeated `c` input at the bootloader with no USB devices attached. The stock-kernel calibration worked. The physical map is unchanged.

This image tests `i8042.dumbkbd=1` on the installed antiX system, suppressing keyboard-device control commands. This is a hypothesis about controller compatibility, not a confirmed cause or fix. Both bootloaders use a total timeout to boot automatically despite stray input. Native AT keyboard input remains enabled. The owner subsequently reported normal controls after removing the battery and rebooting. That does not establish whether this option helped; it remains unchanged. QEMU cannot reproduce the controller fault.

## Sleep

Press and release Power to suspend to RAM; press Power again to wake. Holding it retains the firmware's forced power-off behavior. The button viewer consumes duplicate/wake events to avoid immediately sleeping again. The helper selects `deep` when available, otherwise uses the supported `mem` default; if suspend fails the screen reports it and `/var/log/reader/power.log` records the error. A normal shutdown menu is future work.

The owner confirmed sleep works on the installed antiX system. USB gadget reconnect after sleep remains to be tested with the new driver.

## USB networking

The image now includes a [ported Intel Poulsbo controller driver](iusbc/README.md),
Ethernet gadget, DHCP, a small file/settings web page, and Dropbear SSH. The antiX
kernel did not include a driver for this controller. Revision `reader2` fixes
callback request queueing. USB DHCP, HTTP, file upload/download and SSH login
are now confirmed on the physical Reader with a Fedora host; see
[the investigation](../analysis/usb-network-debug.md).

After installing, connect mini-B to a computer with a data cable. DHCP assigns
the computer `192.168.7.2`; open `http://192.168.7.1`. The host Ethernet profile
must use automatic/DHCP addressing, rather than link-local addressing.
The page uploads/downloads files directly on SSD at `/var/lib/reader/files`,
changes the device name, and sets the root SSH password. Then connect using
`ssh root@192.168.7.1`. No shared password is embedded; SSH login remains locked
until a password is set. Each installation generates its own SSH host key.

DHCP supplies no default gateway or DNS server, so the USB connection does not
replace the computer's normal Internet connection. It does not provide Internet
access to the Reader yet. HTTP/SSH bind only to the USB address. The first host
target is Linux; Windows RNDIS and macOS plug-and-play behavior are unverified.

The on-device USB status reports local setup; the computer must still enumerate
the adapter and obtain a lease. Logs are in `/var/log/reader/{usb,web,ssh}.log`.
Reinstallation replaces uploaded files, settings, passwords and SSH host keys.

## Wi-Fi and web menu

Open `http://192.168.7.1/#wifi`, scan, select a network, enter its password and
choose **Save and connect**. A hidden network can be entered manually. The page
shows association state and the assigned address. **Forget saved network**
removes the stored credentials. One saved network reconnects automatically.
WPA/WPA2 Personal and open networks are supported; WPA3-only, enterprise and WEP
networks are not supported by this small setup.

The Wi-Fi service discovers wireless interfaces through sysfs and starts the
distro's `wpa_supplicant`, with generic nl80211/WEXT fallback. The web backend
uses its [standard control commands](https://w1.fi/wpa_supplicant/devel/ctrl_iface_page.html).
There are no USB IDs, adapter names, interface names or home network credentials
hardcoded into Wi-Fi setup. Adapters still need a driver/firmware supported by
the kernel. The connected adapter works with the existing kernel and firmware.

`dhcpcd` obtains the Wi-Fi address, default route and DNS. It excludes `usb0`,
which keeps its fixed USB address and DHCP server. Chrony synchronizes the
clock automatically for HTTPS; its network server and command ports are off.
The physical Reader obtained a Wi-Fi lease and successfully fetched HTTPS.

Credentials live only in `/etc/wpa_supplicant/reader.conf`, mode `0600`, with a
derived PSK for Personal networks. Passwords/PSKs are not returned by the API.
Factory images contain no saved network. Logs are `/var/log/reader/wifi.log`
and `time.log`. Reinstallation removes the saved network along with other data.

The web interface has hash-linked **Overview**, **Files**, **Apps**, **Updates**,
**Wi-Fi** and **Settings** pages, with browser back/forward navigation. It stays a small
HTML/JavaScript page and a Python standard-library server. Management HTTP and
SSH remain bound to the USB address; Wi-Fi provides the Reader's Internet link.
