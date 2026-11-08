# Reader live boot tests

## antiX 26.1 i386 full

ISO SHA-256 verified before preparing the user's USB. The user subsequently booted it on the Reader and reported that kernel loading completed, followed by `fatal error no usb,cd devices found`. It automatically retried for about 15 seconds and supplied no shell prompt. This does not establish the cause: controller detection, filesystem support, and live-media discovery were not distinguished. At the user's request, move to another live system rather than spend time debugging antiX startup.

## Slax Debian 12.2.0, 32-bit

Candidate downloaded from `https://ftp.linux.cz/pub/linux/slax/Slax-12.x/slax-32bit-debian-12.2.0.iso`.

- Size: 436060160 bytes.
- Published MD5 matched `https://ftp.sh.cvut.cz/slax/Slax-12.x/md5.txt`: `fa75e9b25fc25120589196ac5e4a606d`.
- Locally computed SHA-256: `03b85cd259883f6781b3a3f30ed409b0b6a542b8f510094594c7600bd94e546b`.
- Official USB instructions: `https://www.slax.org/starting.php`.
- ISO is a bootable optical image without a hybrid disk partition table. Prepare the documented MBR/FAT32 file-copy layout instead of writing the ISO directly to USB.
- `write-slax-usb.py` targets the same specifically authorized 15610576896-byte removable USB by serial-number path. Root authentication is needed to partition, format, mount, and install the bundled bootloader.
- Script uses the vendor's bundled extlinux and MBR code; partition boot flag is set directly by sfdisk. Remove `automount` from boot entries to avoid automatically mounting other disks. Other boot options stay as shipped. Do not select copy-to-RAM on the Reader for this first test.
- Preparation script syntax was checked; privileged preparation and Reader boot remain untested until the user runs the script and tests it. This is a diagnostic candidate, not a commitment to the final installed OS.

## antiX 26.1 Core i386: powered-hub retest

Use Core, as requested by the owner: a console-only base without a GUI environment. The original full-edition attempt is historical; the current `write-test-usb.py` now targets **antiX-26.1_386-core.iso**.

- Official download/checksum source: https://antixlinux.com/download/
- ISO source: https://downloads.sourceforge.net/project/antix-linux/Final/antiX-26.1/antiX-26.1_386-core.iso
- Expected SHA-256: `5e07ed85acba1d2c33a7e41b089e80ce203eeb11d81af8c830e64e2e058ead7e`.
- Write from the project root: `sudo python3 analysis/write-test-usb.py`. The writer checks the exact authorized USB identity, capacity, unmounted status, ISO hash, and full ISO-length readback.
- Boot through the powered USB hub. Use the normal live entry without copy-to-RAM or installation. This test does not replace the installed SSD system.
- Linux rejecting the Apple hub's USB power budget is a strong explanation for the earlier live-media discovery failure; this retest will check it. Core boot on the Reader remains untested.
