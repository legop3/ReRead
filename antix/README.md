# ReRead build and system notes

ReRead is a handheld platform built on **antiX 26.1 Core i386**, kernel `5.10.256-antix.1-486-smp`. It uses antiX's package management and normal runit startup, with a Qt X11 button interface and no desktop. There is one maintained OS image.

## Releases and updates

The full installer installs antiX plus the **same `reread` Debian package used
for later updates**. The source of truth is this repository, not a running
Reader. Version is set in [VERSION](VERSION); [release.py](release.py) builds
the platform, Camera, Files and Software packages from source. Personal files, network credentials, host
identity and installed apps are not part of its payload.

Camera updates use the separate `reread-app-camera_<version>_i386.deb` on **Apps**.
To add Camera to an older installation, update the core first, then install that app package.

For online updates and app installation, use the [signed APT repository](../repository/README.md) or the on-device **Software** app.
The source and public key are included starting with ReRead 0.11.0.
Starting with 0.12.0, **Updates → Check for updates** and **Apps → Check for apps and updates**
refresh that repository from the USB web interface. Install the offered platform
release, install an available app, or update an installed app with its update button.
The last successful check is shown; failed checks retain those earlier results.
Checks run only on request, and the normal package job log shows progress and errors.
For a local package upload, open **Updates** in the USB web interface and select
`reread_<version>_i386.deb`. The update installs in place without formatting or
replacing the root filesystem. Wi-Fi settings, uploaded files, app data,
hostname, passwords, SSH host keys and authorized keys remain in place.
The worker runs independently of the HTTP server, with persistent status/logs
so the page can reconnect after the server restarts. A reboot notice covers
background-service/driver changes; reboot with `reboot` over SSH when indicated. CLI: `reader-update install /path/to/release.deb`.

**Apps** installs/updates `reread-app-*` packages, lists installed apps, and removes
them while retaining their data and conffiles. Service apps can be enabled or
disabled persistently. The installer bundles the separately packaged Camera, Files and Software apps; see the
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
tested boot-update release. The antiX userspace remains apt-managed; the ReRead
APT archive supplies platform and app packages through GitHub Pages.

## Maintenance boundaries

The durable foundation is Debian package identities, versions and dependencies,
with application code kept separate from configuration and persistent data.
One platform package definition serves both fresh installs and updates. The
web interface is a front end to apt/dpkg, rather than a separate package system.
Use increasing release versions in `VERSION`; apps use their own independent
versions and declare the ReRead version they require.

This is an early platform, not a complete appliance update system. Updates are
in-place package transactions without automatic rollback or protection against
a power failure mid-update. Changes to the bootloader or kernel ABI still need a tested update
and recovery path. The web catalog uses Debian's Python APT library to read signed
package candidates, with apt-get performing transactions in the existing detached
worker. Cached catalog results keep normal page polling from opening the APT cache
or making internet requests. The foreground launcher discovers standard `.desktop` entries; the shared Qt SDK defines
display, physical input and lifecycle behavior. See [UI notes](ui/README.md).

## Installation

For a released installer, follow the [download and flash instructions](../README.md#install-reread). Any USB stick large enough for the image works; there is no serial-number registration.

On Linux, the repository helper flashes either a downloaded image or an existing local build:

```sh
./make-usb.sh ~/Downloads/reread-installer-usb.img
./make-usb.sh               # Uses out/antix/reread-installer-usb.img
```

It lists USB drives, asks which one to erase, and uses standard `dd` to write it. Unmount the drive's partitions first. To skip the host confirmation for repeated development flashes, pass `--device /dev/sdX --yes`. A partition or internal drive is rejected. No rebuild happens unless `--build` is supplied. The Reader-side installer remains automatic.

Boot the USB through the powered hub. The installer finds the known internal SSD, streams the image from USB, flushes writes and reboots after five seconds. No prompts, full-image checksum scans, decompression pre-tests or readback passes. Decompression and write failures still stop installation. Keep USB attached until **INSTALLATION COMPLETE**.

The SSD image fills the known Reader SSD (4,004,388,864 bytes): a 1 MiB alignment gap, 127 MiB FAT boot partition and the remaining 3,690.9 MiB as ext3 root. There is no unallocated tail or first-boot resize step. The boot partition has space for an initramfs replacement alongside the current file. It fits the Reader's 4 GB SSD. Applications, packages and persistent logs live on SSD; the OS is not loaded into a RAM root. Reinstallation replaces the system and its logs.

## Runtime storage

A fresh ReRead 0.2.1 installation was confirmed on the physical Reader: **2,162.8 MiB free of 3,561.6 MiB** after boot. The full SSD is allocated; the difference from raw capacity is the boot partition and filesystem overhead. Free space changes as packages, caches and user files accumulate.

The 0.6.1 image has approximately **1,875.4 MiB available before first boot**,
measured from its ext3 metadata. Qt, fonts and their distro graphics dependencies
account for most of the increase from the earlier console-only baseline. No desktop
session is installed or started. First boot, updates and personal files change
the available space.

Fresh images remove the live ISO's kernel headers, unused GRUB packages, installer/template tools,
memory tester, AMD graphics firmware and all-locales
bundle using apt/dpkg. ReRead generates `en_US.UTF-8`; other locales can still
be enabled with normal `locale-gen`. Existing translations are retained.

The builder's cached base keeps headers for subsequent module/release builds.
The installed OS keeps antiX’s init packages and Syslinux bootloader, normal kernel modules, broad USB Wi-Fi firmware, package
management, documentation and runtime services. Package archives and generated
apt caches are cleared. Package indexes remain available for app dependency
resolution. New APT index downloads stay compressed; translated package descriptions
and duplicate binary package caches are omitted. Existing unchanged indexes may
stay uncompressed until APT next downloads them.
English UI catalogs are kept; other `/usr/share/locale` translations are excluded
using normal dpkg policy (`/etc/dpkg/dpkg.cfg.d/80-reread-space`). This does not
remove generated libc locales or OCR language data. Delete that policy and
reinstall the relevant packages to restore other UI translations.
Intel PCI-only Wi-Fi firmware and modern Intel DSP firmware are omitted; the
Reader uses USB Wi-Fi and HDA audio. No blanket autoremove or USB-adapter-specific
firmware pruning is used.
This image cleanup applies to fresh installs; uploading the platform `.deb`
does not repartition or purge an existing system.

## Build

Source builds need Docker with its Buildx plugin. The optional Linux USB helper also needs Python 3. Run the build entry point from the repository
root as your normal user with Docker access. It builds/caches the tools in
[the Dockerfile](../image-builder/Dockerfile) and uses a temporary container with
only this workspace mounted at `/work`; no host disks are exposed to the builder.
The host USB writer is separate; `./build.sh` never accesses host disks.

Provide `downloads/antiX-26.1_386-core.iso` (antiX 26.1 Core i386).
The builder checks SHA-256 `5e07ed85acba1d2c33a7e41b089e80ce203eeb11d81af8c830e64e2e058ead7e`
when populating its ISO cache. Also provide the Reader's 23 KB
`build/firmware/CameraCtlr.hex` as described in [camera build inputs](CAMERA.md).
The full stock disk backup is optional research material; only this extracted
camera firmware file is included in the platform package.

```sh
./build.sh                  # Build release files without writing USB
./make-usb.sh --build        # Build, then select and write a USB drive
```

A dedicated Buildx builder named `reread` caches the tool image between runs.
It avoids the embedded Docker builder export stall seen on the development host;
it does not replace your default builder or restart other containers. The workspace mount uses shared
SELinux labeling (`:z`) so it can coexist with a development container.

Outputs in `out/antix/`:

- `reread.img`: full SSD image.
- `reread.img.gz`: compressed SSD payload.
- `reread-installer-usb.img`: bootable automatic installer.
- `reread_<version>_i386.deb`: platform update package.
- `manifest.json`: release version, image sizes and package filenames.

For later platform changes, bump `antix/VERSION` and rebuild just the package.
This uses the matching kernel headers from `build/antix/base-root`, populated by the
initial full build; it does not rebuild an SSD image or touch a device:

```sh
docker run --rm -v "$PWD:/work:z" reread-builder python3 /work/antix/release.py
```

Upload the resulting `.deb` through **Updates**. This is the normal iteration
path once ReRead is installed. Rebuild the full image when preparing a fresh
installer with the latest release. To update just the USB installer around an
already built system:

```sh
docker run --rm -v "$PWD:/work:z" reread-builder python3 /work/antix/package_installer.py
```

`docker run --rm -v "$PWD:/work:z" reread-builder python3 /work/antix/test-packages.py` runs
platform-upgrade and app-management checks in a disposable chroot, including
settings/data preservation and a web-server restart during installation. It
uses synthetic data and never connects to a Reader.

An optional end-to-end installer check is `python3 /work/antix/test-installer.py` inside the builder. It writes only a disposable virtual SSD, checks automatic reboot, and boots that SSD to check `/boot`, input and sleep. A direct Qt X11 boot, card navigation, Camera/Back handoff and two-cycle VM suspend/resume check is `python3 /work/antix/test.py` inside the builder. Pass `--device /dev/kvm` to `docker run` for acceleration when available; QEMU otherwise uses software emulation. SSD installation, display, all 19 controls, and Power-button sleep have been confirmed on the Reader.

## Hardware and maintenance

[reader-keys.h](reader-keys.h) defines the measured controls and stable `READER_*` IDs. Hold value 1 means unlocked; value 0 means locked. The viewer displays it without enforcing a lock. Its initial state at boot remains unchecked.

The custom console font is included in early boot and runtime, with a tall-font fallback.
ReRead includes a [shared native camera engine](CAMERA.md), available through the
on-device Camera app, `reader-camera` CLI and a local HTTP/JSON socket for apps. It
captures stock-cropped 2588×1916 JPEGs with per-device focus calibration, FPGA autofocus and
optional automatic flash. Firmware loading, capture cleanup and sleep/resume are
managed by the platform. The recovered pixel processing matches stock on the
validated fixtures; hardware validation and remaining boundaries are recorded
in the camera notes.

Runtime logs persist under `/var/log/reader`; boot diagnostics use normal Linux tools (`dmesg`, `findmnt`, `/proc/bus/input/devices`). The installed image locks the ISO's shared root/demo passwords; normal operation needs no login. For maintenance, use a live USB to mount/chroot the SSD, or add `rw init=/bin/sh` to the installed boot command to set local credentials.

The USB installer uses the same antiX kernel as the installed system, with the distro's BusyBox, setfont, libc and matching storage modules in a small initramfs. It streams the compressed payload directly from USB. No stock binaries or stock kernel are used.

Fresh images mount the FAT boot partition at `/boot` by UUID. Kernel and initramfs files live there directly, so `update-initramfs` writes the files the BIOS bootloader actually reads. Syslinux names the tested kernel explicitly. Changing the kernel ABI still requires a matched USB-controller build and boot configuration; arbitrary kernel upgrades are not supported. Existing installations with the earlier copied boot files are not repartitioned or migrated by a platform package update. Use the fresh installer for the new boot layout.

## Physical controls

Native helpers use libc/POSIX calls and Linux's input structures, rather than
private syscall wrappers or hand-defined kernel structures. The controls monitor
is still a development interface; a button-operated app launcher is future work.
The measured map is shared in `reader-keys.h` and is independent of screen labels.

The tested boot option `i8042.dumbkbd=1` remains enabled. Early intermittent input
and stray View Toggle events stopped after a full battery removal/power cycle;
that does not prove the option fixed the issue. See the historical
[boot notes](../analysis/live-boot-tests.md). No alternate distribution build or
one-off live-USB flasher is maintained.

## Stock cooling compatibility

Fan control stays with the Reader's embedded controller. ReRead does not
provide a fan-control daemon or change the firmware's temperature thresholds
and fan curve. [fan-reset.c](fan-reset.c) reproduces the stock `HmColbertFan`
reset: set fan mode to 1, set the target to 0, wait two seconds, restore mode 0.
It runs once per boot and after wake through the Reader hardware hook in elogind's sleep lifecycle.
The helper checks Intel Corporation / Colbert DMI identity before touching the
stock SMI ports, and logs to `/var/log/reader/fan.log`. Service restarts do not
repeat the boot reset. The binary is included in the regular ReRead package.

The Reader exposes ACPI thermal/fan devices, but the firmware temperature
method returns a constant 34 C. Do not use `acpitz` as a real temperature source
or infer working critical shutdown merely from its configured trips.
`coretemp` and the EC expose changing sensor readings. See
[the investigation](../analysis/cooling.md) for recovered registers, evidence
and the read-only probe. Sustained-load thermal limits remain unverified.

## Battery and charging

ReRead 0.10.0 uses the distribution's **UPower** daemon for battery readings,
warnings and critical-power actions. D-Bus activates it through its normal
service definition; no desktop is required. The `reader-battery` runit service
is now a signal-driven UI adapter, not a polling/shutdown daemon. It writes
`/run/reader-battery.json` and a readable status line; `GET /api/battery` reads
UPower directly. Charging remains controlled by hardware.

On first migration, existing ReRead thresholds are transferred to
`/etc/UPower/UPower.conf`: normally low warning 12%, critical warning 7%, and
PowerOff action 6%. An already-customised UPower threshold policy is retained.
UPower supplies its own 20-second action grace period. ReRead shows an imminent
action warning instead of inventing a second countdown. The old
`/etc/reader/power.json` is no longer used after migration. Later package updates
do not rewrite UPower settings. Restart the UPower daemon after editing its
configuration. Migration records `/var/lib/reader/upower-configured`.

The Reader can report Discharging on AC when charging is paused; the UI labels
that combination Not charging. That presentation adjustment does not override
UPower warning/action decisions. The API retains nullable legacy charge fields
and adds standard UPower energy, power and time-to-empty readings.

Actual charger removal/reconnection, UPower state and live charging were checked
on the Reader. The physical battery has not been drained to critical to exercise
UPower's shutdown action. Userspace protection does not run during S3. See
[the current integration](../analysis/linux-battery-apps.md) and
[the original hardware investigation](../analysis/battery.md).

## Audio

Speaker playback works through the normal ALSA HDA/IDT driver. ReRead
makes the physical Volume Up/Down buttons control the real master level and
shows it on the diagnostic screen. Mixer settings use standard `alsactl` storage at
`/var/lib/alsa/asound.state`, preserved across updates, and restored at startup
and after normal Reader sleep. New installs default to 35% mapped volume. Existing JSON volume preferences migrate once to ALSA state and the old file is removed only after a successful save.

`reader-audio play /path/to/file.wav` provides foreground playback for future
apps; `reader-audio status` returns live JSON, and `up`, `down`, `set 35`, `mute`
and `unmute` control volume. Playback uses ALSA's internal `MID` card default
PCM. The helper adds no audio server. Its log is `/var/log/reader/audio.log`.
PCM WAV playback is checked; compressed-format decoding is future app work.

The owner confirmed speaker playback and the physical volume controls.
Headphone routing still needs a physical check. A capture device exists, but
the test recording produced no useful signal; a working microphone is not
confirmed. See [audio findings](../analysis/audio.md).

## Sleep

Power requests use elogind/login1 and the Linux suspend-to-RAM path. Press Power
to sleep and again to wake; firmware long press retains forced shutoff. Settings /
Power provides Sleep now, Restart and Shut down. Elogind selects deep sleep and
runs Reader-specific Camera/fan/audio/backlight preparation and restoration via
its hardware hook. A failed Camera preparation cancels sleep. Logs are
`/var/log/reader/power.log` and `/var/log/reader/elogind.log`.

Settings / Power also provides automatic sleep after 2, 5, 10 or 30 minutes
without input, default Off, on battery and AC. `auto_suspend_seconds` is saved in
`/etc/reader/display.json`; the PAM-managed X session adapter reports activity
to login1 and generates elogind's idle-action configuration. This works across
apps, including apps that use no ReRead helper. Standard idle inhibitors keep
playback/downloads or package operations awake. Global volume shortcuts run in
triggerhappy; udev hwdb applies the calibrated control mapping. Apps share input
nonexclusively and do not own the Power/volume policy.

The owner confirmed deep sleep/wake, restored camera preview and USB management
reconnection on the installed antiX system; see
[the validation record](../analysis/reread-ui-validation.md).

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
The **Files** page embeds the bundled Copyparty browser for folder-based transfers
directly on SSD at `/var/lib/reader/files` (full-screen browser on USB port 8080).
Apps share that same ordinary directory; no data partition is reserved. See the
[storage policy](STORAGE.md) and [upstream package provenance](THIRD_PARTY.md). The management page
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

Open `http://192.168.7.1/#wifi` to manage saved profiles. Scan or enter an SSID,
choose security and enter its password, then **Save network** or **Save and connect now**.
Saving another profile retains all existing networks. **Edit** changes a specific
profile; a blank password keeps its current PSK when the SSID/security are unchanged.
Each profile has a hidden-network flag, automatic connection switch and priority
(higher values are preferred). **Connect**, **Enable/Disable** and **Forget** act
on that profile only. Connecting explicitly enables that profile; enabled alternatives
remain available for fallback and future boots.

WPA/WPA2 Personal and open networks can be created/edited here. Existing profiles
with other security can be selected or removed but not edited in this form.
The standard `wpa_cli` control commands manage network IDs and `save_config`
persists them in wpa_supplicant's own format; there is no ReRead profile database.
Saved profiles require a plugged-in adapter and running supplicant to edit.

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
**Repositories**, **Wi-Fi** and **Settings** pages, with browser back/forward navigation. It stays a small
HTML/JavaScript page and a Python standard-library server. Management HTTP and
SSH remain bound to the USB address; Wi-Fi provides the Reader's Internet link.

## On-device software management

The default **Software** application is an independent Qt executable in the
`reread-app-software` Debian package, with its own desktop entry and hicolor SVG.
Use its button menu to check repositories, install a ReRead system update, browse
installed/available applications, install/update them, or remove an app while keeping
its data. The operation page shows the persistent package job and scrollable log.
An app removal has a Cancel-first confirmation. The existing Home power menu handles
restarting after a system update.

Software calls the local `reader-software` CLI, which starts the same detached
`reader-update` worker as the web interface. It needs no web server or USB host,
adds no daemon, and does not implement another package manager. Jobs continue if
the foreground app exits or a core update restarts Home. APT and runit still own
dependencies, installation and app services.

Repository management is available at `http://192.168.7.1/#repositories`; see the
[source-file contract](../repository/README.md#additional-repositories).

Focused checks (inside the builder unless indicated):

```sh
python3 antix/test-base-management.py
python3 antix/apps/software/test-navigation.py
# On a host with Node, Playwright and Chromium:
node antix/test-web-packages.cjs
```

The backend test uses real APT parsing and a disposable wpa_supplicant with the
`none` driver on the container's loopback interface. It does not change the Reader's
networks. The native navigation test uses a temporary CLI fixture inside the builder;
it refuses to replace an installed `reader-software` command. Screenshots go under
`build/repository/`. The browser test intercepts API requests and makes no device changes.
