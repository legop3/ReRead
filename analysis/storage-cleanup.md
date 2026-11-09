# Runtime storage cleanup

Applied to the physical Reader after its 0.14.0 install, and included in the next
source build (0.14.1). The published 0.14.0 installer was not rebuilt or replaced.

The Reader uses Syslinux, USB Wi-Fi and Intel HDA audio. APT removal simulations
showed that GRUB packages, `firmware-iwlwifi` and `firmware-sof-signed` could be
removed without removing ReRead or Qt. The installed iwlwifi driver advertises
only PCI aliases; this removal does not select a particular USB Wi-Fi adapter.
Realtek, Atheros and Broadcom USB firmware packages were retained.

Removed GRUB/EFI packages and unused Intel PCI Wi-Fi/DSP firmware with apt/dpkg.
Removed unused non-English `/usr/share/locale` catalogs, preserving English and
`locale.alias`. Standard dpkg path exclusions prevent reinstalling those catalogs.
Generated libc locales, OCR data, documentation, manpages, kernel/modules,
microcode and the Mesa/LLVM stack were retained. A simulation showed removing
Mesa/LLVM would remove Qt/ReRead. Build/DKMS tooling was also retained.

APT translated-description indexes and redundant binary package caches were
removed. Standard APT configuration disables recreating them and keeps future
index downloads compressed. A successful APT refresh retained existing unchanged
uncompressed package indexes; they will be replaced normally when downloaded
again. No package/dependency indexes were deleted to force a download.

Measured available root-filesystem space:

- Before: 1,834,291,200 bytes (1.71 GiB).
- After: 2,320,760,832 bytes (2.16 GiB).
- Recovered: 486,469,632 bytes (463.93 MiB).

`/usr/share/locale` fell from 104 MiB to 960 KiB; APT lists from 97 MiB to 59 MiB.
After disabling generated binary package caches, `/var/cache/apt` was 12 KiB.
Python APT opened the package catalog, found ReRead installed and reported zero
broken dependencies. `dpkg --audit` produced no output. The Reader UI, camera and
USB services remained running; management status and the file manager returned
HTTP 200. The Syslinux config and HDA sound card remained present. No reboot or
new audio playback test was performed. User files were not deleted or moved.

Implementation: `antix/trim.py` shares the cleanup with fresh builds; build policy
is installed before fetching distro dependencies. Normal APT/dpkg configuration
files under `antix/storage/` are also included as platform conffiles. The CLI in
`trim.py` can apply the cleanup to an installed Reader, with its adjacent
`storage/` directory available; it checks for ReRead and Syslinux before proceeding.
The cleanup is not run from a dpkg maintainer script, avoiding nested APT jobs.
