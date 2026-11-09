# ReRead

A multipurpose handheld platform for the Intel Reader, built on **antiX 26.1 Core 32-bit**. Home uses a button-operated icon grid; system settings are grouped into categories. Apps are separately packaged Linux programs with their own UI and standard desktop-entry icons; device and drawing helpers are optional. There is no desktop or browser on the device. Large text and a fixed safe inset on every side account for the overlapping bezel.

The first app is **Camera**: live preview, full-resolution JPEGs, Photo/Document modes and optional flash. Preview and saved photos default to 180° rotation for holding the bottom-facing camera forward. **OK/Select or Shoot** takes a photo; Options changes camera settings, View shows the last photo, and Back returns Home.

Power-button sleep, stock fan behavior, battery monitoring, ALSA speaker audio and physical volume control use normal Linux services. Settings provides brightness, sound, idle dimming, optional auto-suspend, connection information and power options. USB Ethernet supplies DHCP, SSH and a management web interface for files, Wi-Fi provisioning, apps and updates. Management daemons run only while connected to a USB host; camera streaming stops when no app needs it. Use a powered USB hub for installation.

## Install or update

ReRead is built from this repository as an installable OS and a versioned platform package. Fresh installs and later updates use the same `reread` package. Applications use `reread-app-*` Debian packages and standard `.desktop` launcher entries. The installer includes `reread-app-camera`; the management Apps page installs, updates and removes apps independently.

Existing installations can update in place through the [signed APT repository](repository/README.md), preserving files, Wi-Fi settings, credentials and app data. In the USB web interface, **Updates → Check for updates** finds platform releases; **Apps** browses available apps and updates installed ones. Both pages also accept local `.deb` uploads. App removal retains app data and configuration. Checks and installations happen on request.

For a **fresh installation**, connect one registered USB stick, unmount its
partitions, then run from the project folder:

```sh
./make-usb.sh
```

This prepares the Docker builder, builds the SSD image, platform and Camera packages, assembles the bootable installer,
and writes it to the registered USB stick. It stops on a build failure before
writing USB. Docker and Python 3 are required; `sudo` is used only for the USB
writer. The source ISO and extracted camera firmware requirements are in the
[build instructions](antix/README.md#build).

To build release files without writing a USB stick, use `./make-usb.sh --build-only`.
To flash an already-built installer, use `sudo python3 antix/write-usb.py`.

Boot the USB through the powered hub. Installation starts automatically: stream the OS onto the known Reader SSD, flush writes, then reboot after five seconds. Keep USB attached until **INSTALLATION COMPLETE**. There are no prompts, full-image scans, or readback checks. Booting this installer replaces the current SSD installation.

The writer checks the USB identity to avoid writing to the wrong host disk. Both registered sticks are accepted; connect one at a time.

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
