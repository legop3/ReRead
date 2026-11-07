# Reader Workbench image builder

**Historical desktop experiment.** The active system is the [minimal POC](../poc/README.md). This directory remains as reference, and its Dockerfile and `prepare-stock.py` helper are still used for POC build inputs. The desktop instructions below describe the earlier approach.

This builds a general-purpose Debian 12 i386 desktop and a separate BIOS USB installer. The desktop fits inside the Reader's 4 GB internal SSD. It does not run the original OCR application as its interface.

The USB installer has an independent RAM root filesystem. Default boot uses the Reader's stock 2.6.27 kernel and compatible userspace, with a newer Debian kernel as an alternative. Both run the same installation menu. Losing or failing to discover the USB payload leaves an interactive recovery shell available; it does not leave the user at a fatal live-distribution screen.

The installer still needs Linux to read the USB carrying the payload. The stock kernel is a compatibility fallback, not proof that the user's keyboard hub works. No claim of successful Reader hardware installation is made until that test happens.

## Build environment

Builds run in a Docker container without privileged mode, host block devices, or host filesystem mounts other than this workspace. QEMU operates on regular files. The complete stock disk dump and extracted boot/kernel resources in this workspace are build inputs.

```sh
docker build -t reader-image-builder image-builder
docker run --rm --device=/dev/kvm -v "$PWD:/work:Z" reader-image-builder sh /work/image-builder/build.sh
```

The first build downloads Debian packages. Logs and working files are under `build/`; deliverables are under `out/`. The package versions used are recorded in `out/packages.tsv`. Reuse of an existing configured root is controlled by its `.reader-configured` marker. `/dev/kvm` accelerates virtual-machine checks; omit that device option on hosts without KVM, where tests fall back to slower software emulation.

Scripts:

- `configure-root.sh`: packages, user account, desktop configuration, networking, boot modules.
- `build-system.py`: ext4 disk image, boot menu, BIOS bootloader installation inside QEMU.
- `build-installer.py`: two small RAM environments, compressed payload, FAT32/MBR USB disk image.
- `installer-init.sh` and `installer-menu.sh`: actual on-device installer.
- `test-installer.py`: virtual USB/hub detection, disk selection refusal, and full write/readback test.
- `write-installer-usb.py`: writes the USB image to the specific stick authorized in this session.

## Boundaries

The first installer only accepts the known internal SSD model `SSDPAPS0004G1`, capacity 7,821,072 sectors. It rejects the source disk, USB-backed targets, mounted targets, partitions, and removable disks. A physical installation requires both entering the internal disk path and typing `INSTALL <that path>`. Checksums are embedded in the RAM environment; the payload is validated before writing and the entire image-length target prefix is read back afterward.

The internal image is 3,584 MiB, smaller than the recorded 4,004,388,864-byte SSD. It leaves the remaining tail unallocated. There is no repartitioning of another connected disk, automatic target write, or network download during device installation.

Hardware-specific work remains: LCD timing/scaling, physical-button mapping, camera and flash integration, mini-USB gadget support, and validation of cooling/power/suspend. The desktop includes basic graphics and text-mode boot entries for initial testing. Generic desktop behavior in QEMU cannot validate those Reader-specific features.
