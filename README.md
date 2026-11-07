# Intel Reader Hack

Turning a 2009 Intel Reader into a multipurpose, button-operated Linux handheld. The goal is a small native interface for its physical controls, camera, speaker, and USB gadget port.

The current proof of concept boots the stock kernel into a readable **BOOT OK** screen and displays button events. A separate USB installer writes the system to the internal SSD automatically, verifies the image, and reboots.

## Current status

| Component | Status |
| --- | --- |
| Internal boot and console display | Working on the Reader |
| Physical controls | Some buttons register through Linux evdev; others need investigation |
| Installation from USB storage | Working on the Reader with a powered USB hub |
| Automatic reboot after installation | Tested in QEMU; Reader test pending |
| Installed root filesystem | Small RAM filesystem; SSD-backed root is next |
| Camera, flash, audio, and USB gadget functions | Stock support investigated; custom integration pending |

The device examined here has an Atom Z520, approximately 512 MB RAM, and a 4 GB SSD. The proof uses its original 32-bit `2.6.27-4-lpia` kernel to retain a path to the device-specific hardware support.

## Start here

- [POC build and installation instructions](poc/README.md)
- [Hardware findings and project progress](RESEARCH.md)
- [Display investigation](analysis/display-photo-notes.md)
- [Earlier live-USB experiments](analysis/live-boot-tests.md)
- [Original research prompt](DEEP_RESEARCH_PROMPT.md)

**The installer automatically replaces the internal installation when booted.** It requires exactly one internal SSD matching the known Reader model and capacity. Keep your own stock disk backup. The USB-writing scripts are pinned to the original owner's USB stick; review the device identity and size checks before adapting them to another stick.

The Apple keyboard's built-in USB hub caused Linux to reject the flash drive for exceeding its reported power budget. A powered USB hub resolved this on the tested Reader.

## Folder layout

| Path | Purpose |
| --- | --- |
| `poc/` | Active minimal system, installer, button viewer, and VM tests |
| `analysis/` | Curated investigation notes and historical USB preparation scripts |
| `image-builder/` | Historical desktop experiment; its Dockerfile and stock-extraction helper also support the current POC |
| `evidence/` | Local stock dump and photos, excluded from version control |
| `build/`, `out/` | Local working files and generated images, excluded from version control |
| `downloads/`, `archive/` | Local downloads and earlier artifacts, excluded from version control |

## Building

This is a hardware research workspace, not a complete distribution or a turnkey image build from a fresh checkout. Builds need an owner-provided stock dump and extracted kernel, modules, and compatible utilities. Those files are deliberately excluded from version control. See [the build notes](poc/README.md#build) and [local evidence requirements](evidence/README.md).

VM checks exercise installation, target refusal, checksums, reboot, and input display. They do not validate the Reader's camera, missing buttons, or other custom hardware.

## Next steps

1. Move the installed root filesystem onto the SSD and retain persistent settings and logs.
2. Map the remaining physical controls.
3. Build a small button-operated launcher.
4. Integrate camera, audio, networking, and gadget functions as capabilities are proven.

## Included materials

Stock software, firmware dumps, personal photos, extracted filesystems, and generated disk images stay local. The console font assets derive from the kbd project; their provenance and upstream license are in [poc/assets/](poc/assets/README.md). A project-wide license has not been selected.
