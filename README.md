# ReRead

A multipurpose handheld platform for the Intel Reader, built on **antiX 26.1 Core 32-bit**.

## Install ReRead

**[Download the USB installer from Releases](https://github.com/legop3/ReRead/releases)** — choose `reread-installer-usb.img` from the newest release containing an installer.

You need an Intel Reader, a **1 GB or larger USB stick**, and a **powered USB hub**. Installation erases the Reader’s SSD; back up anything you want to keep.

1. Download `reread-installer-usb.img` to your computer.
2. Write it to your USB stick with [balenaEtcher](https://etcher.balena.io/) (**Flash from file → Select target → Flash**), or another disk-image writer. This erases the USB stick. No formatting or file copying is needed.
3. Connect the stick to the Reader through the powered hub and select USB boot in its BIOS boot menu, using a USB keyboard if needed.
4. Installation runs automatically and reboots. Keep the stick connected until **INSTALLATION COMPLETE**, then remove it during the reboot countdown.

No source checkout, Docker, stock disk dump or registered USB drive is required. The published 0.14.0 installer includes **Camera** and **Software**; update ReRead to get the independent **Files** app. New source builds include all three.

### Linux command line

From a source checkout, you can also flash a downloaded image:

```sh
./make-usb.sh ~/Downloads/reread-installer-usb.img
```

The helper lists USB drives by name and size, lets you choose one, and asks before erasing it. Unmount its partitions first. It uses Python 3, `lsblk`, `dd` and `sudo`; it does not build the OS or download anything.

### Build from source

[Build prerequisites and firmware input](antix/README.md#build) are needed only for source builds.

```sh
./build.sh                   # Build release files into out/antix/; no USB flashing
./make-usb.sh                # Flash the existing local installer; choose a USB drive
./make-usb.sh --build        # Build, then choose a USB drive and flash
```

For repeated development flashes to a known drive, use `./make-usb.sh --device /dev/sdX --yes` (replace `/dev/sdX` with the actual whole USB drive). The Reader-side installation remains automatic.

## Updates and apps

Existing installations can update in place through the [signed APT repository](repository/README.md), preserving files, Wi-Fi settings, credentials and app data. In the USB web interface, **Updates → Check for updates** finds platform releases; **Apps** browses available apps and updates installed ones. Both pages also accept local `.deb` uploads. App removal retains app data and configuration. Checks and installations happen on request. **Repositories** manages APT sources and signing keys; **Wi-Fi** manages multiple saved networks, priorities and automatic connections.

## The platform

Home uses a button-operated icon grid; system settings are grouped into categories. Apps are separately packaged Linux programs with their own UI and standard desktop-entry icons; device and drawing helpers are optional. There is no desktop or browser on the device. Large text and a fixed safe inset on every side account for the overlapping bezel.

Default apps are **Camera**, **Files** and **Software**. [Files](antix/apps/files/README.md) browses folders, previews photos/text and deletes files or folders with confirmation; it is a separately managed app. Software manages system updates and application installs/removal directly with the Reader’s buttons, without a USB connection. Camera provides live preview, full-resolution JPEGs, Photo/Document modes and optional flash. Preview and saved photos default to 180° rotation for holding the bottom-facing camera forward. **OK/Select or Shoot** takes a photo; Options changes camera settings, View shows the last photo, and Back returns Home.

Power-button sleep, stock fan behavior, battery monitoring, ALSA speaker audio and physical volume control use normal Linux services. Settings provides brightness, sound, idle dimming, optional auto-suspend, connection information and power options. USB Ethernet supplies DHCP, SSH and a management web interface for files, Wi-Fi provisioning, apps and updates. **Files** embeds Copyparty for folders, drag-and-drop uploads, previews, rename/move/delete and downloads. Apps and both file browsers share the ordinary `/var/lib/reader/files` directory; [storage policy](antix/STORAGE.md). Management daemons run only while connected to a USB host; camera streaming stops when no app needs it. Use a powered USB hub for installation.

## Linux foundation

Prefer standard Linux components throughout the project. Evaluate existing
services, interfaces and formats before building replacements; keep custom code
focused on Reader-specific hardware support and the ReRead experience. Apps remain
independent Linux programs with optional shared helpers.

ReRead uses antiX/runit, apt/dpkg packages, ALSA audio and mixer state, Linux evdev/udev buttons, elogind power policy and inhibitors, triggerhappy global hotkeys, UPower battery policy over Linux power_supply, and wpa_supplicant networking. Native helpers use libc and normal Linux interfaces. GLib/GIO handles desktop entries and app launching. Qt Widgets/LinuxFB provides the embedded UI. Reader-specific code covers its physical control map, display configuration, USB controller driver, camera USB/FPGA protocol and stock fan reset. The native camera engine uses libusb and libjpeg-turbo, with recovered stock image processing. The installer also uses antiX; no stock kernel or vendor executables are required.

## Project files

- [Build and system notes](antix/README.md)
- [On-device UI and controls](antix/ui/README.md)
- [Application contract and SDK](antix/apps/README.md)
- [Shared storage and web file manager](antix/STORAGE.md)
- [Bundled file-manager provenance](antix/THIRD_PARTY.md)
- [UPower and GIO integration](analysis/linux-battery-apps.md)
- [Linux power/input migration and hardware validation](analysis/linux-power-migration.md)
- [Initial Linux integration research](analysis/linux-power-integration.md)
- [Shared camera interface and firmware input](antix/CAMERA.md)
- [Verified physical control map](analysis/button-map.md) and [raw measurements](buttons.md)
- [Hardware research](RESEARCH.md)
- [Display investigation](analysis/display-photo-notes.md)

`antix/` contains the only OS build, release packaging, installer, button interface, web management and font assets. `image-builder/Dockerfile` provides its build tools. Downloads, generated images, stock backups and personal photos remain local under ignored directories. Builds need the small extracted `CameraCtlr.hex` firmware file; the full stock backup is optional research material.

The Reader has an Atom Z520, approximately 512 MB RAM and a 4 GB SSD. The installed OS uses antiX kernel `5.10.256-antix.1-486-smp`, normal runit startup and apt/dpkg. All 18 physical button labels were verified on the Reader during calibration. Hold pressed means unlocked; released means locked.

The console font used during boot has its provenance and license in [antix/assets/](antix/assets/README.md). A project-wide license has not been selected.
