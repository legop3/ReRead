# Stock camera resource ledger

Offline shell observations, 2026-11-07. This is a resource/configuration/ELF pass accompanying the separate REA camera-code investigation. Commands used: `cat`, bounded `rg`, `readelf -d/-V/-Ws`, `nm -D -C`, `strings`, `sha256sum`, and an Intel HEX record/checksum parser. No stock executable was run, no live Reader was accessed, and no firmware was programmed in this pass. Paths below are relative to `analysis/stock-root/` unless stated otherwise.

## Startup and firmware boundaries

**Observed configuration:** `etc/udev/rules.d/85-colbert.rules:2` handles every Cypress VID `04b4` USB add event by calling `/lib/udev/cypress-eeprom <idProduct>`. The helper has these exact branches:

| Argument | Action |
| --- | --- |
| `1005` | Explicit no-op. |
| `0083` | Explicit no-op. |
| `init` | `/sbin/fxload -t fx2 -I /usr/share/usb/CameraCtlr.hex -D <USB-device-node>`, then poll up to ten seconds for `lsusb` output containing `1005`. |
| Anything else | Load `Vend_Ax.hex`, then run `/sbin/eeprom -i` under the comment “repair blank or corrupt EEPROM”. |

**Interpretation:** `init` is the normal volatile FX2 controller-firmware path; EEPROM repair is separate and must not be reproduced as a generic unknown-device fallback. The `init` argument cannot come directly from a hexadecimal USB PID. Identifying its exact caller requires the camera-code investigation; the helper alone does not prove call order.

The helper's USB-node selection greps all `04b4:` devices and is not robust if several Cypress devices exist. The re-enumeration check also greps only `1005`, not a vendor/product pair. A modern implementation should select the known camera's exact USB device.

`etc/init.d/Colbert` changes working directory to `/opt/colbert/bin`, sets `LD_LIBRARY_PATH=../lib:../../loquendo/bin:../../abbyy/bin:../../opencv/lib:../../gh/lib`, and starts `Reader`. `opt/colbert/bin/Reader` is a shell supervisor that starts `./C3`, restarts it according to `c3rval`, and clears `../tmp/*`. This is the observed supervisor sequence, not a complete boot-service ordering: rc links and root X startup files were not in this partial extraction.

`etc/pm/sleep.d/30colbert` invokes `/lib/udev/cypress-eeprom 0083` on resume. Although its comment says “reload firmware”, that argument is an actual no-op in the shipped helper. Do not infer a required firmware reload from the comment.

**Historical runtime corroboration:** `var/log/syslog:434-463` records C3 “Camera controller is unprogrammed!”, two “Can't find a programmed Cypress!” messages, USB `1-5` disconnect/re-enumeration, then Cypress **Version 5, Revision 6**, FPGA **Version 58**, and BIOS 3.3. The sequence takes about ten seconds. Lines 470 onward later record “Image captured”. These are old stock logs, not observations of current ReRead. The USB descriptors historically triggered maxpacket and string-descriptor warnings but enumeration completed.

## FPGA and image resources

`opt/colbert/fpgaprog.log` records image `/mnt/tmp/FPGA_stable.rpd`, requested version `0x58`, a read returning bytes `00 58`, and **no update because the current FPGA version already matched**.

`var/lib/dpkg/info/colbert-fpgaprog.postinst` is a persistent FPGA-update path: outside a squashfs first-install environment, read `/tmp/FPGA_VERSION` and invoke `/sbin/fpgaprog -v <version> -f /tmp/FPGA_stable.rpd`, temporarily turning the display off with `vbetool dpms`. An empty version defaults to `0xff`; the script comments describe `0xff` as forcing an update. This installer script is not a camera-start prerequisite and should not be executed to initialize capture.

The package file list names `/tmp/FPGA_VERSION`, `/tmp/FPGA_stable.rpd`, and `/tmp/fpga_update.sh`. These files are absent from the current extraction, and `analysis/inventory/root-list.txt` has no matching entries. A package's old file list is not proof the bitstream remains in the dump. The FPGA part number/manufacturer and sensor model were **not established** by this bounded search of camera strings, extracted configs, logs, and inventories.

`usr/share/usb/CameraCtlr.hex` is valid Intel HEX: 580 data records, one EOF record, 7,849 data bytes at addresses `0x0000..0x1ea8`, with all record checksums valid. `Vend_Ax.hex` has 283 data records, one EOF, 3,359 data bytes within `0x0000..0x0d92`, likewise checksum-valid. This validates file structure, not firmware behavior or target identity.

`usr/share/usb/a3load.hex` includes comments explaining an EZ-USB second-stage loader using vendor request `0xA3`, and identifies `Vend_Ax` as the development loader capable of I2C boot EEPROM writes via request `0xA2`. Comments support the distinction above but are not a decompilation of `CameraCtlr.hex`.

`opt/colbert/etc/FactorySettings.xml:135-148` offers shooting modes `bulk_shoot_mode` and `photo_shoot_mode`, defaulting to bulk, and flash choices `automatic` and `off`, defaulting to automatic. Other UI XML references describe focus/shoot sounds and icons; they do not establish sensor-register settings.

## Stock-library compatibility boundary

`opt/colbert/lib/libcolbert-camera.so` is stripped **ELF32 little-endian i386**. Exported signatures include `Start()`, `Stop()`, `TakePicture(vector<unsigned short>&, boost::function<void()>)`, `TakePicture(std::string, boost::function<void()>)`, `sensor::Init()`, `sensor::SetDefaults(unsigned short,unsigned short)`, and BMP encoders. A wrapper must recover actual calling conventions and object semantics before calling them.

Its direct DT_NEEDED entries are:

- `libcolbert-common.so`
- `libboost_iostreams-gcc42-1_34_1.so.1.34.1`
- `libboost_thread-gcc42-mt-1_34_1.so.1.34.1`
- `libstdc++.so.6`, `libm.so.6`, `libgcc_s.so.1`, `libpthread.so.0`, `libc.so.6`

It also imports **legacy libusb-0.1 `usb_*` symbols without a direct libusb DT_NEEDED entry**, including bus discovery, open/close, interface claim, configuration, halt clearing, and bulk read/write. `opt/colbert/bin/C3` supplies `libusb-0.1.so.4` as a direct dependency. A small standalone runner would have to link/load that ABI explicitly and make its symbols visible; loading the camera DSO alone is insufficient.

`libcolbert-common.so` adds `libxml2.so.2` and Boost filesystem/date_time 1.34.1 to the dependency closure. It also imports unversioned `shm_open`/`shm_unlink`; check resolution on the target libc rather than assuming the camera DT_NEEDED list is complete.

Camera ELF version requirements are GLIBC 2.0/2.1.3/2.4, GLIBCXX 3.4/3.4.9, CXXABI 1.3, GCC 3.0/3.4. Common adds GLIBC up to 2.4 and CXXABI 1.3.1. **Inference:** modern i386 libc/libstdc++ may provide these old symbol versions, but that alone does not prove ABI compatibility. Stock string signatures use the old C++ string ABI; compile a direct C++ wrapper with `_GLIBCXX_USE_CXX11_ABI=0` or avoid passing C++ objects across the boundary. Boost 1.34.1 exact ABI is an additional constraint; symlinking a modern Boost library under an old filename is not a solution. No compatibility load or capture was attempted here.

The current extracted tree is **partial**: stock Boost and libusb files are not present on disk in it. The original full-file inventory `analysis/inventory/root-list.txt:14174-14550` does list exact Boost gcc42 1.34.1 shared libraries under `/usr/lib`; lines 15239-15248 list `/lib/libusb-0.1.so.4.4.4`, `/lib/libusb-1.0.so.0.0.0`, and their symlinks. Thus these dependencies are recoverable candidates from the source dump, not evidence that the stock system lacked them. Stock dpkg status also records Intel-built `libusb-compat` 0.1.0-1 and `libusb` 1.0.0-1 packages. Their actual current linkage requires inspecting the recovered files.

A portable protocol backend may avoid proprietary Boost/common-library baggage, but whether it is practical depends on the separate code investigation of image transfer, sensor setup, autofocus and flash. This ledger does not claim that backend exists.

## Artifact identity

| Artifact | SHA-256 |
| --- | --- |
| `opt/colbert/lib/libcolbert-camera.so` | `b8cf21e44aad76ea7874cd7812a65a916ff0be20b06c65cd7cd9b596b5262b6b` |
| `opt/colbert/lib/libcolbert-common.so` | `6cfa9ff5099636567bfa7e8bd8a40494602884fae11670291f9a1bedf500b10b` |
| `usr/share/usb/CameraCtlr.hex` | `61d003fca740a8ea4901c73f6a8fc2a07fbb3d1d8b203231e51fda1138a9b23e` |
| `usr/share/usb/Vend_Ax.hex` | `7d2942d761595d4f659742fbc8d50ed5ab9d7e65783a87b299c5035933bc1aef` |
| `lib/udev/cypress-eeprom` | `eb8c0b4ebfbdf388dbe5b86143ea3f779bfc75e279f410eaae2022079b307124` |

## Checklist status

- Resource/configuration startup sequence: **partially answered**; supervisor, udev, and historical re-enumeration recorded; camera-code init order belongs to the main REA pass.
- RAM firmware versus persistent writes: **answered at script boundary**; `init` path distinguished from EEPROM repair and FPGA installer.
- Exact firmware/library/executable paths and stock ABI: **answered for extracted resources**, with original inventory identifying missing shared-library candidates.
- Sensor/FPGA silicon identity: **unresolved** by this bounded pass; firmware version does not identify silicon.
- Stock-library compatibility: **constraints identified**, runtime loading/capture untested.


## Chassis OEM field supplies the autofocus offset (2026-11-08)

Additional **GNU objdump/readelf shell observations**, with no shared REA session changes or live hardware access:

- Common-library `colbert::dmi::chassis::OemInfo()` at **VA 0x3f020** runs `boost::call_once` with an initializer at **0x3f2c0** and returns a cached **unsigned 32-bit** value from **0x4ea14**.
- The initializer at **0x3f759** references rodata **0x426b0**, containing this complete command:

  ```sh
  echo $(($(dmidecode -t3 | sed -rn '/Chassis Information/,${s/.*OEM Information: //p}')))
  ```

  At **0x3f79c**, an `ipipestream` reads its output; **0x3f7c3** invokes `std::istream::_M_extract<unsigned int>`. Thus the source is the numeric **SMBIOS type 3 Chassis OEM Information** field, with the shell arithmetic converting the printed hexadecimal number to decimal. It is not the chassis version/name/serial sysfs string. The once-only cache means a failed initial read would not automatically be retried by this accessor; error behavior needs further analysis.
- Camera-library `detail::Start()` at **0x12ade** calls `OemInfo()`, **0x12ae3** executes `movzx eax,ax` (explicitly retain its **low 16 bits**), and **0x12af0-0x12af7** writes them to FPGA register **0x109**. The following log at **0x12b37**, referencing rodata **0x20faf**, is **“Wrote focus offset of ”**. This establishes the stock software's interpretation as a focus-offset input. It does not establish physical units, signed interpretation inside the FPGA, calibration range, or how manufacturing originally populated the field.

The direct read-only query on the Reader is `dmidecode -t3`; look for `OEM Information:` under `Chassis Information`. If dmidecode is absent, the following Python reads only the kernel-exposed SMBIOS table and extracts the same DWORD at formatted type-3 offset **0x0d**, then displays the value the camera writes:

```python
from pathlib import Path
import struct

data = Path('/sys/firmware/dmi/tables/DMI').read_bytes()
pos = 0
while pos + 4 <= len(data):
    kind, length = data[pos], data[pos + 1]
    if length < 4 or pos + length > len(data):
        raise ValueError('Malformed SMBIOS structure')
    if kind == 3:
        if length < 0x11:
            raise ValueError('Chassis structure has no OEM DWORD')
        raw = struct.unpack_from('<I', data, pos + 0x0d)[0]
        print(f'chassis_oem=0x{raw:08x} focus_offset=0x{raw & 0xffff:04x}')
    if kind == 127:
        break
    end = data.find(b'\x00\x00', pos + length)
    if end < 0:
        raise ValueError('Missing SMBIOS string terminator')
    pos = end + 2
```

This parser has not been run against the Reader here. A missing sysfs table or unusually short type-3 structure requires inspecting the actual firmware data, not substituting an unrelated `/sys/class/dmi/id/chassis_*` field. Multiple chassis structures would be ambiguous for the original stock shell command too; report all observed values before selecting one.

### Sensor and other autofocus clues

A focused shell disassembly of the camera library finds:

- `sensor::Write()` at **0x119a0** and `sensor::Read()` at **0x11bc0** use FPGA I2C target **0x5d**, one-byte register indices, and two-byte register data.
- `sensor::SetDefaults()` at **0x119e0** writes reset register `0x0d=1`, PLL-related candidates `0x10=0x50` and `0x11=0x6404`, then `0x0d=0`, `0x01=0x32`, `0x02=0x0a`, `0x03=height-1`, `0x04=width-1`, and `0x07=0x1f82` among other settings. “PLL-related” is an interpretation of the register pattern, not a recovered register-name table.
- This interface/register pattern is a **Micron/Aptina MT9P031-family candidate**, not a confirmed model. Establishing a model requires an authoritative register-map comparison and ideally the read-only chip-ID register value; no such live query or datasheet comparison was performed in this subtask.
- `sensor::Init()` at **0x11bf0** sends camera command **0x1a** with zero payload. This may delegate sensor setup to FX2 firmware; command semantics beyond the call parameters are not established here.
- The bounded stock config/inventory search found **no separate autofocus calibration file**. UI XML focus entries are sound/icon names. This negative result is limited to the current extracted configs, `analysis/inventory/root-list.txt`, camera-library strings, and stock syslog; it is not a full filesystem content scan.
- Historical `var/log/syslog:4315,4346` reports “Bad focus due to timeout”; line **15187** reports “Focus is locked/prelocked before exposure is locked. Refocusing...”. These document stock runtime handling of focus failure, not the current hardware's condition.

The OEM-derived low-16-bit offset is therefore the strongest recovered per-unit calibration source. Preserve it rather than guessing a constant when implementing a modern backend.
