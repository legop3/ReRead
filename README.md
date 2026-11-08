# ReRead

A multipurpose, button-operated Linux handheld built on **antiX 26.1 Core 32-bit**. It starts a small native interface with no desktop. Buttons, Power-button sleep, USB Ethernet with DHCP, web file transfers and SSH work. The web interface has Overview, Files, Apps, Updates, Wi-Fi and Settings pages. Wi-Fi connects using a supported USB adapter and remembers one network. Camera and audio integration come later.

SSD installation, boot, display, all 19 controls and sleep work on the Reader. The large console font accommodates its unusual display. Use a powered USB hub for installation.

## Install or update

ReRead is built from this repository as an installable OS and a versioned platform package. Fresh installs and later updates use the same `reread` package. Future applications use `reread-app-*` packages; the Apps page can install, update and remove them. No apps are bundled yet.

For an existing ReRead installation, upload `out/antix/reread_<version>_i386.deb` on the web **Updates** page. This updates the platform in place, preserving files, Wi-Fi settings, credentials and app data. App removal also retains app data and configuration. There is no hosted app store or automatic update feed yet.

For a **fresh installation**, build the images using the [build instructions](antix/README.md#build), then flash the installer:

Connect the configured USB stick and run from this folder:

```sh
sudo python3 antix/write-usb.py
```

Boot the USB through the powered hub. Installation starts automatically: stream the OS onto the known Reader SSD, flush writes, then reboot after five seconds. Keep USB attached until **INSTALLATION COMPLETE**. There are no prompts, full-image scans, or readback checks. Booting this installer replaces the current SSD installation.

The writer checks the USB identity to avoid writing to the wrong host disk. Both registered sticks are accepted; connect one at a time.

## Project files

- [Build and system notes](antix/README.md)
- [Verified physical control map](analysis/button-map.md) and [raw measurements](buttons.md)
- [Hardware research](RESEARCH.md)
- [Display investigation](analysis/display-photo-notes.md)

`antix/` contains the only OS build, release packaging, installer, button interface, web management and font assets. `image-builder/Dockerfile` provides its build tools. Stock inputs, downloads, generated images and personal photos remain local under ignored directories. See [evidence requirements](evidence/README.md).

The Reader has an Atom Z520, approximately 512 MB RAM and a 4 GB SSD. The installed OS uses antiX kernel `5.10.256-antix.1-486-smp`, normal runit startup and apt/dpkg. All 18 physical button labels were verified on the Reader during calibration. Hold pressed means unlocked; released means locked.

The goal is a button-operated launcher with camera, speaker, network and gadget capabilities as their hardware support is established. The console font's provenance and license are in [antix/assets/](antix/assets/README.md). A project-wide license has not been selected.
