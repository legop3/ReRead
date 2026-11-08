# Intel Reader investigation

Initial investigation: 2026-11-06. The original findings below came from offline inspection of `evidence/reader.img` and focused web research. At that stage no Reader hardware was tested or image binaries executed. The source image was opened read-only; extracted files are under `analysis/`.

The project target is a multipurpose, button-operated handheld with a small custom interface. Camera/upload/audio are possible features; a desktop is outside the intended experience. Preserve the original hardware support while building the new system.

## Current progress: 2026-11-08

The maintained product is **ReRead**, based on antiX Core i386 with normal runit,
apt/dpkg, ALSA, evdev and standard Linux power interfaces. SSD boot, all controls,
sleep, USB Ethernet/web/SSH, USB Wi-Fi, battery reporting and speaker playback
have been verified on the Reader. ReRead 0.3.1 packages the shared camera
service and firmware, with live web preview and an app/SSH capture interface.
Camera research produced complete
648×480 stream frames and 2592×1920 stills through Python/libusb, with FPGA focus
lock and automatic flash. Focus accuracy and color/exposure quality remain unfinished. See the
[platform camera interface](antix/CAMERA.md) and
[camera protocol and capture report](analysis/camera-protocol.md).
See [ReRead system notes](antix/README.md) for current behavior and limitations.

The build and automatic installer use antiX only. The stock-kernel prototype and
one-off live-distribution flashers are retired. The powered hub resolved the
observed USB power-budget rejection. The evidence below remains a hardware
reference; it is not a plan to keep or wrap the stock application.

The measured map covers all 18 push buttons and Hold. Hold value 1 means unlocked; value 0 means locked. Initial Hold state reporting remains unchecked. See [the measured control map](analysis/button-map.md).

## What the dump establishes

| Component | Evidence | Implication / remaining uncertainty |
| --- | --- | --- |
| CPU | Recorded `var/log/dmesg` identifies Atom **Z520 at 1.33 GHz**, two logical CPUs | Do not assume the Z540 reported by some articles. Confirm the actual current unit with `/proc/cpuinfo`; the log could be a historical factory boot. |
| Memory | Same log: `504336k/518656k available` | Consistent with **512 MB installed**, with some reserved. Enough for a small native UI and capture/upload client; use a server for substantial processing. |
| Architecture | Kernel `CONFIG_X86_32=y`; application ELF files are Intel i386; packages use `lpia` | Start with 32-bit x86 tools. `lpia` is Ubuntu's old low-power Intel package architecture, not ARM. |
| OS | `etc/lsb-release` | **Ubuntu 8.10 (intrepid)**. |
| Kernel | Boot files and GRUB configuration | **2.6.27-4-lpia** with device-specific support. |
| Application | dpkg status | `colbert-app` and `colbert-primary` **1.0.0-7978**. Internal project name: **Colbert**. |
| Graphics | `etc/X11/xorg.conf` | Poulsbo `psb` X driver, 24-bit depth, unusual **962x544** internal display mode. |
| Camera / flash | Camera library, udev helper, camera firmware | Custom Cypress FX2 USB plus FPGA path; high-level library functions survive as dynamic symbols. |
| Speaker | Normal HDA/IDT STAC9205 driver; stock library references GStreamer `alsasink` | [Speaker playback and physical volume control verified](analysis/audio.md); headphone routing and a useful microphone signal remain unverified. |
| USB device port | `iusbc.ko`, kernel config, `usb_client_helper` | Stock mass storage uses a real Linux USB gadget controller. Ethernet and serial gadget modules are also supplied. The ported controller and Ethernet gadget now work on antiX; see [USB findings](analysis/usb-network-debug.md). |
| Wi-Fi | Kernel config disables `WIRELESS_EXT`, `WLAN_PRE80211`, and `WLAN_80211` | A conventional USB Wi-Fi adapter is **not a plug-and-play addition to this stock kernel**. |
| Other networking | `CONFIG_INET=y`; USB Ethernet host drivers and `g_ether.ko` included | USB Ethernet gadget to a host, or a supported wired USB Ethernet adapter, are better first candidates. |
| Other hardware | `HmColbertFan`, `HmColbertWDT`, battery/ACPI log | [Cooling investigation](analysis/cooling.md): ACPI temperature is hardcoded; changing sensors and fan state are available through the EC. [Battery status and protection](analysis/battery.md) now use standard Linux readings; watchdog policy remains separate work. |

These are observations of files and recorded logs, not fresh measurements of the device.

## The display mystery

The top of the stock X configuration says:

```text
# Colbert-specific configuration
# 1.  Poulsbo video driver
# 2.  Special resolution for internal display (resolution is doubled
#     due to FPGA pixel decimation.
```

The screen's configured mode is exactly `962x544`, not `960x544`. Do not silently round it. This configuration does not supply a full modeline or establish the exact sampling pattern.

This is strong evidence for why a generic live desktop looked wrong. There is hardware processing between the GPU output and physical LCD, in addition to the normal problem of desktop controls being too small. The stock UI presumably accounts for the path; we have not yet traced how it draws or scales its content. A generic DPI adjustment might help readability but does not establish correct timing or sampling.

First record the working stock X mode/timings and photograph a known test pattern. Then compare the replacement system's connector, mode, framebuffer dimensions, and visible result. Do not start by forcing a physical-panel resolution or copying this old `psb` configuration into a modern driver stack.

The stock system also supports a SiS USB-to-VGA adapter through a separate `SISLayout`. That is optional external display support, not evidence of a built-in VGA socket.

## Camera: difficult, but unusually good clues

Useful files in `analysis/stock-root/`:

- `opt/colbert/lib/libcolbert-camera.so`
- `usr/share/usb/CameraCtlr.hex` and `Vend_Ax.hex`
- `lib/udev/cypress-eeprom`
- `etc/udev/rules.d/85-colbert.rules`
- `opt/colbert/fpgaprog.log`

The library is stripped, but exported C++ names include:

```text
colbert::camera::Start()
colbert::camera::Stop()
colbert::camera::TakePicture(...)
colbert::camera::SaveBmp(...)
colbert::camera::StreamServer::StreamServer(...)
colbert::camera::fpga::ChargeStrobe(bool)
colbert::camera::fpga::FireStrobe(unsigned char)
colbert::camera::fpga::EnableFocusLed(bool)
colbert::camera::sensor::Init()
```

`StreamServer` is a C++ class name; it does not establish that the stock device exposes a network stream.

Static REA/Hopper analysis of `usb::FindCypress()` at `0xe960` shows vendor ID `0x04b4` and branches for product IDs `0x1005` and `0x0083`. The udev script independently handles those same product IDs. The helper uses `fxload -t fx2` with `CameraCtlr.hex`, and also contains an EEPROM repair branch. This supports a custom userspace camera interface rather than assuming `/dev/video0` or UVC support.

Initial REA evidence: `ev_fda1d2b2c2c3b65ffb18f8db62d838543451ab5a1088374c5a0de822eb51abbf`. Full result and snapshot: `analysis/inventory/camera-find-cypress.json` and `camera.rea-snapshot.json`. See the [later protocol report](analysis/camera-protocol.md) for live corroboration and remaining unknowns.

The recorded FPGA programming log reports version `0x58` already matching the supplied image and therefore no update. This is historical evidence, not an instruction to program it.

The research probe now loads the stock FX2 RAM firmware and captures complete
648×480 Bayer frames using modern Python/libusb on antiX. The live Reader reports
controller 5.6 and FPGA `0x59`; no persistent FPGA update was performed. The old
Intel library is used as protocol evidence, not executed as a runtime dependency.
The probe now also reads per-device BIOS focus calibration, waits for FPGA focus
lock, captures complete 2592×1920 stills and operates the flash. FFmpeg provides
basic research preview conversion. ReRead 0.3.0 packages this backend with shared
capture ownership, a web test page and sleep/resume handling, using libdc1394 and
Pillow for conversion. Color/exposure quality and difficult-focus recovery still
need work.

## Networking and the gadget port

`lib/udev/usb_client_helper` coordinates the stock UI with Linux `g_file_storage`. It unmounts `/dev/sda3` before presenting it to the host. Preserve that ownership rule: do not let host and Reader simultaneously write the same FAT filesystem.

The kernel supplies:

```text
CONFIG_USB_GADGET_IUSBC=y
CONFIG_USB_IUSBC=m
CONFIG_USB_ETH=m
CONFIG_USB_ETH_RNDIS=y
CONFIG_USB_G_SERIAL=m
CONFIG_USB_FILE_STORAGE=m
```

The stock modules provided the starting evidence. ReRead now uses the antiX
kernel with the ported `iusbc` controller and standard `g_ether`, USB DHCP,
HTTP file transfers and Dropbear SSH, verified with the owner's Fedora host.
USB and Wi-Fi serve separate roles: USB management at `192.168.7.1`, generic
USB Wi-Fi for the Reader's Internet link. The Wi-Fi path uses the distro's
wpa_supplicant/dhcpcd, with no adapter IDs or network credentials embedded in
ReRead. See [current networking notes](antix/README.md#usb-networking).

## Product direction

ReRead is a multipurpose handheld platform with installable applications,
not a stock-app wrapper or a single-purpose upload appliance. Hardware findings
here support that platform. Application concepts and the on-device launcher
will be designed separately once the system features are in place.

## Image and extraction record

Source: `evidence/reader.img`, **4,004,388,864 bytes**.

SHA-256: `7caaba050df8ca886834de5cab8ee451e9b4c41c27d708d45c876d7df8b38d51`

| Partition | Start sector | Sectors | Format / purpose |
| --- | ---: | ---: | --- |
| 1 | 63 | 48,132 | ext2, boot, about 23.5 MiB |
| 2 | 48,195 | 3,325,455 | ext3, root system, about 1.59 GiB |
| 3 | 3,373,650 | 4,016,250 | FAT32, user data, about 1.92 GiB |
| 4 | 7,389,900 | 417,690 | ext3, `/opt/appdata`, about 204 MiB |

512-byte sectors. Partition 3 was not extracted or searched for personal content. Partition 4 was extracted, but its reader database was not inspected. Selected root files and the boot partition were extracted with 7-Zip, without mounting filesystems or replaying journals. Some relative symlinks were rejected by 7-Zip; **the extracted directory is an analysis subset, not a bootable root filesystem**. The ext filesystems also have unused trailing partition bytes, reported by 7-Zip as data after the archive.

`analysis/inventory/` contains the hash/geometry JSON, full partition file listings, extracted-file logs, camera symbol names, and REA evidence. `analysis/partitions/` holds derived copies of partitions 1, 2, and 4.

## Public research and leads

- [Intel's historical Reader page](https://timeline.intel.com/2009/the-intel-reader): official product identity and intended camera-to-speech use.
- [Intel product brief, archived by Howard](https://media.howard.com/literature/Technology/Howard/archives/co_brands/intel/irbrief.pdf): official specifications include a 4.3-inch screen, 5 MP autofocus camera, 4 GB SSD, USB-A and mini-B, integrated speakers, and nominal camera focus range of 10 cm to about 1 m. The specified short focus range is relevant if considering ordinary photography.
- [CTL firmware-update instructions](https://support.ctl.net/hc/en-us/articles/229725867-Updating-the-firmware-on-the-Intel-Reader): vendor documentation for an update from 1.00 to 1.01, with an old FTP package URL. Documentation was accessible; package download and integrity were not verified.
- [2021 hardware-hacking thread](https://www.reddit.com/r/hardwarehacking/comments/rlpkkb/intel_reader_softwarehardware_hacking/): an owner reports F11 USB boot and imaging the disk; a commenter points to `Intel_Reader_1.02.zip` in a CTL Box folder. **Community lead only**. The linked Box folder was not accessible through the research tool; 1.02 availability is unverified.
- [Historical discussion pointing to GPL sources](https://www.opennet.ru/openforum/vsluhforumID3/60832.html): identifies Intel Download Center ID **18112** and describes a roughly 681 MB source archive. This is an unverified archive-recovery lead, not confirmation the custom camera/FPGA code was released.

The bounded search found product documentation, a source-release lead, and a hacking discussion, but no verified complete alternate-OS port. It does not establish that nobody has built one. A research prompt for following these leads is in [the original research prompt](analysis/original-research-prompt.md).
