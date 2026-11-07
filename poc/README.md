# Minimal Reader proof of concept

This build does exactly two things after boot: shows **BOOT OK** and displays input-device/button events. It has no desktop, browser, login screen, or mouse interface. Normal use does not need a keyboard. A keyboard is needed only to select USB boot. Installation itself needs no typing or confirmations.

The system uses the stock Reader kernel with a small RAM filesystem. It does not need to discover or mount a boot drive after the BIOS loads its kernel and RAM image. The installer loads only its small environment into RAM. It loads the stock USB-storage and FAT drivers, mounts the USB partition read-only, checks the OS image, and streams it directly onto the internal SSD. The OS payload is not embedded in the installer initrd. Keep the USB connected until installation finishes.

## Install

1. From the project root on your computer, connect the USB stick configured in `poc/write-usb.py` and run:

   ```sh
   sudo python3 poc/write-usb.py
   ```

2. Boot that USB on the Reader using your keyboard. The default boot entry starts automatically after three seconds.
3. The installer finds the internal **SSDPAPS0004G1** SSD, checks the USB payload, writes the image, verifies the entire installed image, and **reboots automatically** after a five-second countdown. There are no prompts or confirmations. Keep USB connected until **INSTALLATION VERIFIED**; it can then be removed during the countdown.
4. Let the Reader boot from its internal SSD into the button tester. If its firmware prefers USB at restart, remove the stick during the countdown to avoid starting the installer again.

The writer is deliberately pinned to the original owner's USB by-id path and capacity. Before using a different stick, review and adapt both checks in `write-usb.py`; the script will erase that stick.

**Booting this USB automatically replaces the internal installation.** The installer still requires exactly one non-USB, nonremovable, unmounted whole disk with the Reader SSD's known model and capacity. Missing/corrupt payloads, zero matching targets, or multiple matching targets stop installation and open a diagnostic shell. It never retries a failed installation automatically. Logs are in `/tmp/drivers.log`, `/tmp/storage.log`, and `/tmp/readback.log` in RAM. Run `/bin/sh /install` from that shell to explicitly retry; `/buttons --poweroff` shuts down.

For each iteration, rebuild and run the same USB-writing command, boot the stick, wait for verification, and let it reboot into the installed system. The installer checks a build-specific payload ID and SHA256, so mixing files from different builds fails before writing. It selects the target by identity rather than a disk letter.

## Outputs

Files are under `out/poc/`:

- `reader-poc.img`: **64 MiB** internal-drive disk image.
- `reader-poc.img.gz`: compressed copy of that image.
- `reader-poc-installer-usb.img`: **128 MiB currently**, growing with the payload: complete bootable installer USB image.
- `manifest.json`, `SHA256SUMS`, and test logs/results.

The USB image is for the USB stick; the system image is what its installer writes internally. Remaining internal storage is left unpartitioned for later work. The old desktop attempt has been moved under `archive/desktop-attempt/` and is not part of this proof of concept.

VM validation passed with 128 MiB RAM: automatic installation from USB behind a USB 1.1 hub, complete readback, automatic reboot into the installed system after removing USB during the countdown, and a separate installed boot/button test without USB. Wrong-model and ambiguous targets, missing payloads, and corrupt payloads were refused without writing the image region. Results and serial logs are in `out/poc/`. The user also confirmed storage installation on the physical Reader using a powered USB hub. The reboot update still needs a Reader test.

## What the screen means

`Inputs` is the number of Linux input-event devices opened, which can include a keyboard, power button, and other devices. A last-event record shows the device name, numeric Linux key code, and pressed/released/repeat state. Familiar codes have generic names such as UP; these are not a verified map of the Reader's physical controls. Unknown codes are shown numerically. No button is assigned an application action in this build.

If built-in buttons produce no events, that is a useful test result: their path may require additional stock-specific handling. The program does not assume every control is exposed as Linux evdev. Input records are kept in `/tmp/buttons.log` in RAM and disappear at power-off.

The startup tries a large console font, then a narrower tall font if the console cannot accept it. The user confirmed the previous boot/button proof works on the Reader, with only some physical buttons registering. Storage installation works on the Reader with a powered USB hub; the reboot update still needs a Reader test. This remains a boot/input proof, without camera or network integration.

## Build

Builds require local stock inputs that are excluded from version control; see [evidence/README.md](../evidence/README.md). The current scripts expect the workspace mounted at `/work` in a running container named `intelreader-validator`, plus host GCC with i386 code generation enabled. The container can be created with:

```sh
docker build -t intelreader-builder:local image-builder
docker run -dit --name intelreader-validator --device=/dev/kvm \
    -v "$PWD:/work:Z" intelreader-builder:local sh
docker exec intelreader-validator python3 /work/image-builder/prepare-stock.py
```

Omit `--device=/dev/kvm` if KVM is unavailable; QEMU falls back to software emulation. The `:Z` bind-mount label is for SELinux hosts. The extraction helper uses the specific disk geometry documented for this Reader's image.

The Debian builder container provides packaging tools; none of the desktop files or Debian desktop packages are included in these images. `buttons.c` compiles to a static i386 executable using direct Linux syscalls, with no libc or GUI dependency:

```sh
mkdir -p build/poc
gcc -m32 -Os -nostdlib -static -fno-pie -no-pie -fno-stack-protector -fno-builtin \
    -Wall -Wextra -Werror -o build/poc/buttons poc/buttons.c
build/poc/buttons --self-test
docker exec intelreader-validator python3 /work/poc/build.py
docker exec intelreader-validator python3 /work/poc/test.py
```

Build inputs include the previously extracted stock kernel, modules, BusyBox, compatible small utilities/libraries, and font assets. Both font assets scale the host kbd package's `Mik_8x16.gz` bitmap font and include explicit Unicode maps for ASCII text. The large font doubles width and height; the fallback doubles height. The font retains its upstream licensing.

## Growth beyond RAM

The installer reads the compressed payload from USB in a stream and does not need RAM equal to the image size. Its small initrd contains the kernel modules and utilities needed for USB discovery. The USB filesystem cache is reclaimable memory.

The **installed OS currently still uses a RAM root filesystem**. Before adding large applications/assets, move that root filesystem to an internal SSD partition, keeping a small boot initrd to load storage drivers and switch to the SSD root. Changing installer transport does not change the installed runtime. Mapping the remaining buttons and a small button-operated launcher can follow without introducing a desktop.

## USB hub compatibility

On the Reader, Linux rejected the stick behind the Apple keyboard hub with `rejected 1 configuration due to insufficient available bus power`. The user confirmed that a powered USB hub fixes this and storage installation succeeds. Use that powered hub for further iterations. The installer does not automatically override USB power-budget checks.
