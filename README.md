# ReRead

A multipurpose, button-operated Linux handheld built on **antiX 26.1 Core 32-bit**. It starts a small native interface with no desktop. Buttons, Power-button sleep, the stock fan reset, USB Ethernet with DHCP, web file transfers and SSH work. Battery level, external power and low-charge warnings appear on the Reader and web interface; critical charge triggers a delayed clean shutdown while awake. The web interface has Overview, Files, Apps, Updates, Camera, Wi-Fi and Settings pages. Wi-Fi connects using a supported USB adapter and remembers one network. Speaker playback and persistent physical volume control work through ALSA. The shared camera service provides live web preview and full-resolution JPEGs with FPGA autofocus and optional flash; focus consistency and color/exposure quality remain unfinished.

SSD installation, boot, display, all 19 controls and sleep work on the Reader. Fresh images use the full 4 GB SSD and omit live-installer/build baggage. The large console font accommodates its unusual display. Use a powered USB hub for installation.

## Install or update

ReRead is built from this repository as an installable OS and a versioned platform package. Fresh installs and later updates use the same `reread` package. Future applications use `reread-app-*` packages; the Apps page can install, update and remove them. No apps are bundled yet.

For an existing ReRead installation, upload `out/antix/reread_<version>_i386.deb` on the web **Updates** page. This updates the platform in place, preserving files, Wi-Fi settings, credentials and app data. App removal also retains app data and configuration. There is no hosted app store or automatic update feed yet.

For a **fresh installation**, connect one registered USB stick, unmount its
partitions, then run from the project folder:

```sh
./make-usb.sh
```

This prepares the Docker builder, builds the SSD image and platform package, assembles the bootable installer,
and writes it to the registered USB stick. It stops on a build failure before
writing USB. Docker and Python 3 are required; `sudo` is used only for the USB
writer. The source ISO and extracted camera firmware requirements are in the
[build instructions](antix/README.md#build).

To build release files without writing a USB stick, use `./make-usb.sh --build-only`.
To flash an already-built installer, use `sudo python3 antix/write-usb.py`.

Boot the USB through the powered hub. Installation starts automatically: stream the OS onto the known Reader SSD, flush writes, then reboot after five seconds. Keep USB attached until **INSTALLATION COMPLETE**. There are no prompts, full-image scans, or readback checks. Booting this installer replaces the current SSD installation.

The writer checks the USB identity to avoid writing to the wrong host disk. Both registered sticks are accepted; connect one at a time.

## Linux foundation

ReRead uses antiX/runit, apt/dpkg packages, ALSA audio and mixer state, Linux evdev buttons, ACPI/power_supply battery readings, and wpa_supplicant networking. Native helpers use libc and normal Linux interfaces. Reader-specific code covers its physical control map, display font, USB controller driver, camera USB/FPGA protocol and stock fan reset. Camera conversion uses packaged libdc1394 and Pillow. The installer also uses antiX; no stock kernel or vendor executables are required.

## Project files

- [Build and system notes](antix/README.md)
- [Shared camera interface and firmware input](antix/CAMERA.md)
- [Verified physical control map](analysis/button-map.md) and [raw measurements](buttons.md)
- [Hardware research](RESEARCH.md)
- [Display investigation](analysis/display-photo-notes.md)

`antix/` contains the only OS build, release packaging, installer, button interface, web management and font assets. `image-builder/Dockerfile` provides its build tools. Downloads, generated images, stock backups and personal photos remain local under ignored directories. Builds need the small extracted `CameraCtlr.hex` firmware file; the full stock backup is optional research material.

The Reader has an Atom Z520, approximately 512 MB RAM and a 4 GB SSD. The installed OS uses antiX kernel `5.10.256-antix.1-486-smp`, normal runit startup and apt/dpkg. All 18 physical button labels were verified on the Reader during calibration. Hold pressed means unlocked; released means locked.

The goal is a button-operated launcher with camera, speaker, network and gadget capabilities as their hardware support is established. The console font's provenance and license are in [antix/assets/](antix/assets/README.md). A project-wide license has not been selected.
