# ReRead camera protocol and capture

Investigation started 2026-11-07, capture verified 2026-11-08 on Daniel's Reader
running ReRead 0.2.1 / antiX, Linux 5.10.256-antix.1-486-smp.

**Platform integration:** ReRead 0.4.0 replaces the Python proof-of-concept with
one native USB/focus/framing/processing engine. Current usage is documented in
[the camera interface](../antix/CAMERA.md); stock processing equivalence is in
[the native processing report](camera-native-processing.md). Earlier observations
below remain historical evidence, rather than a second maintained camera stack.

## What works

The stock camera can deliver images through normal Linux libusb without running
Intel's application, old camera library, or old Boost dependencies. The initial research
probe retrieved a 1,866,240-byte stream. A single
queued USB transfer contained consecutive complete **648 × 480** Bayer frames,
311,040 words apart, with intact line markers. The grayscale preview was visually
inspected and shows a recognizable scene. Further work on 2026-11-08 captured
complete **2592×1920** stills both without flash and with automatic flash. Each
complete still contains 9,953,280 bytes, one frame marker and 1,920 intact lines.
The FPGA reports focus lock, and the resulting printed-page images were visually
inspected. Daniel also confirmed the orange focus light and flash physically fired.

This is a research proof, not an installed ReRead camera service or application.
Focus/still/flash transport now works in the research probe. Calibrated color,
exposure quality, focus behavior across distances and reliable platform lifecycle
handling remain unfinished. The only runtime additions were Debian `fxload`,
`python3-usb`, and `usbutils`, plus research files under
`/var/lib/reader/research/camera/`. No OS reinstall was needed.

## Reproduce on the Reader

Use the platform `reader-camera` CLI or Camera web page for normal captures.
For raw diagnostic captures, [camera-probe.cpp](camera-probe.cpp) links the same
native hardware source, rather than preserving the old Python camera engine:

```sh
# In the i386-capable builder (workspace mounted at /work):
g++ -m32 -std=c++17 -O2 -pthread analysis/camera-probe.cpp \
  antix/camera_hardware.cpp -lusb-1.0 -o build/antix/camera-probe
# Copy that binary to the Reader, then pause the service before direct USB use:
reader-camera suspend
./camera-probe --still page.raw
reader-camera resume
```

Add `--flash` after the output filename for a flash still; `--stream stream.raw`
retrieves a three-frame diagnostic burst. Firmware must already have been loaded
by the platform service. The probe does not program EEPROM or FPGA storage.
The old Python raw probe and diagnostic gamma converter have been removed.
The offline [stock comparison runner](compare-camera-processing.py) renders raw
fixtures through both stock and ReRead native processing.

## Live observations

| Item | Observed value |
| --- | --- |
| Physical USB path | `1-5` |
| Before controller firmware | `04b4:0083` |
| After RAM firmware | `04b4:1005`, USB high speed, configuration 1/interface 0/alternate 0 |
| Controller version | Reply `05 06` → 5.6 |
| FPGA version register `ffff` | `0059` → `0x59` |
| Sensor address/register | I2C address `5d`, register `00` returns `1801` |
| FPGA register `1005` | Changes with state; **not** a second version register |
| After capture | LED register `0017=0000`, FPGA mode `0012=0001` |

The old dump's logs report FPGA `0x58`; the actual Reader reports `0x59`.
We did not change its persistent FPGA firmware. Sensor and FPGA silicon part
numbers remain unidentified; a register value alone is not a confirmed model.

The camera exposes vendor-specific bulk endpoints, not UVC, and no video node
was found in the initial live inventory. This establishes the need for a small
hardware-specific backend. It does not require replacing standard Linux USB,
networking, storage, audio, or package management.

## Recovered USB protocol

All endpoints below are bulk, using interface 0:

| Endpoint | Purpose | Stock timeout |
| --- | --- | --- |
| `01` OUT | Commands | 1000 ms |
| `81` IN | Command replies | 1000 ms |
| `82` IN | Image words, little endian | Capture requests use 5000 ms |

`SendMessage` builds `command, body_length, body`. With a nonzero subcommand,
body becomes `10, subcommand, inner_length, payload`, where `inner_length` is
payload length plus 2. With a nonzero I2C address, that address is prepended to
payload before constructing this inner header. Queries wait 10 ms before reading
the response. Register addresses and values inside commands are **big endian**;
image words on endpoint `82` are **little endian**.

| Operation | Command bytes / response |
| --- | --- |
| Controller version | `0f 00`, read 2 bytes |
| FPGA register read | `0d 02 regHi regLo`, read big-endian 16-bit value |
| FPGA register write | `10 07 10 01 06 regHi regLo valHi valLo` |
| I2C write | `10 07 10 03 06 addr reg valHi valLo` |
| I2C read request | `10 05 10 04 04 addr reg`, then read FPGA register `fffe` |
| Trigger | `10 04 10 05 03 bool` |
| Start/stop stream | Old L1 format `0a 01` / `0a 00`, then **sleep 1 second; no reply** |
| Still trigger | Old L1 format `08 00 64`; not yet exercised |

The sensor read/write path and FPGA register read/write commands were verified
live. The old L1 functions bypass `SendMessage` and omit its length field.
`cypress::SetState` calls `sleep(1)`, not `usb::Read(1)`; confusing that call with
a reply read caused an initial probe timeout and was corrected against assembly.

## Initialization and image framing

`sensor::SetDefaults(2592,1920)` resets/configures the sensor at I2C address `5d`.
`detail::Start()` then programs volatile FPGA registers. `EnterStreamMode()`
sets sensor registers `06=0008`, `20=c040`, `22=0003`, `23=0003`, `1e=4006`,
opens the shutter through I2C `6b:00=0602`, sets FPGA mode `12=0002`, focus LED
`17=0002`, `107=0003`, pulses Trigger `1,0,0,1` and starts streaming.
The probe records the actual default register sequence in source.

The previously omitted FPGA register `109` is now recovered: stock `OemInfo()`
reads the SMBIOS type 3 Chassis OEM Information DWORD, and camera startup keeps
its low 16 bits while logging “Wrote focus offset of ”. The probe reads the same
field directly from `/sys/firmware/dmi/tables/DMI`, avoiding a `dmidecode` process.
Daniel's value is `0020`, written and read back successfully. This is per-device
calibration, not a constant to hardcode for other Readers. See the detailed
[resource derivation](camera-resources.md).

`usb::ClearData()` is `usb_clear_halt(handle,82)`. The capture probe reproduces
that before reading. Small separate 64 KiB reads lost pixels between transfers.
A single 1,866,240-byte read, matching the stock approach of requesting three
frames together, produced intact frames. The first frame after startup can be
incomplete; the preview decoder skips it.

In each 16-bit image word:

- Bits 0–11 contain the Bayer intensity.
- Bit 12 marks a line's first pixel (observed exactly every 648 words).
- Bit 15 marks a frame's first pixel; consecutive complete frames are 311,040 words apart.
- Bits 13–14 also contain metadata; their exact meaning remains unresolved.

The still dimensions exported by the library are **2592 × 1920**, now verified
by complete live captures. Still line markers occur every 2592 words.

## Focus and full-resolution still sequence

Stock's internal focus helper is at `12da0`, called from `detail::SetMode(2,...)`.
The FPGA performs the lens/exposure loop. The host writes FPGA `12=2`, `107=3`,
stops USB streaming with old L1 `0a 00`, then polls registers `1000` and `1005`
every 250 ms. It reads each twice, checks that they differ, and requires a one-hot
low 12-bit status field. When bit 15 is set in both registers, it logs focus lock
and reads sensor gain register `35`. The normal stock timeout is 10 seconds.

The probe reproduced that path. Ambient shot: raw `e3fc`, status `c080`, gain
`0a60`. Successful flash shot: raw `f3f8`, status `c800`, gain `3460`.
These status transitions plus readable page images corroborate operation, but
are not a calibrated optical focus test. Stock also contains pre-lock/stuck-read
fallback branches; the prototype currently times out instead of reproducing
all those recovery paths.

After focus, stock sets sensor `1e=4198`, `06=0400`, `20=4040`, `22=0`, `23=0`,
closes the shutter with I2C `6b:00=0601`, sets FPGA `12=1`, and waits one second.
`detail::TakePicture` sets sensor gain `35=000c` before the shot. Capture clears
halt on `82`, sends old L1 `08 00 64`, and reads the still in one transfer.
The probe then disables strobe triggering/charging, stops streaming and turns
the focus light off. Final register readback: `12=1`, `17=0`, `109=20`.

The first flash fired (Daniel confirmed) but returned 9,693,258 bytes with missing
rows. The corrected path preallocates/touches the entire receive buffer **before
triggering**, matching stock's vector allocation order. It also matches stock's
`ScopedSchedulerChange(100)` during the bounded still read: `SCHED_FIFO` at the
maximum priority, restored in `finally`. The stream path uses stock's 20 percent
priority mapping. The next flash capture returned all 9,953,280 bytes with every
line marker intact. This establishes a successful corrected capture, not an
endurance result or proof of which timing difference caused every lost byte.

`ScopedSchedulerChange` in `libcolbert-common.so` at `2cd90` maps the percentage
linearly between `sched_get_priority_min/max(SCHED_FIFO)`, saves the current
policy/priority, and restores them in its destructor. Python uses the standard
`os.sched_*` interfaces for that same operation; no custom scheduler is involved.

## Light, shutter, focus and flash boundaries

Static analysis recovered these controls:

| Feature | Stock operation | Validation |
| --- | --- | --- |
| Focus LED | FPGA register `17`: enabled `2`, disabled `0` | Register readback and Daniel's orange-light observation confirmed; disabled afterward |
| Shutter | I2C address `6b`, register `00`: open `0602`, close `0601` | Both used in successful stream/still setup; mechanical behavior not independently checked |
| Sensor | I2C `5d`, register + 16-bit value | ID and gain register read/write verified |
| Lens DAC | `dac::Write` shifts value left 4 and writes I2C address `0c` | Static only; not exercised |
| Strobe charging | `SendMessage(10, subcommand=6, payload=04/05)` for enable/disable | Charging enabled during focus and disabled after capture; flash fired |
| Strobe enable | Same subcommand, payload begins `03`, followed by mode/duration | `03 01` enables stock automatic mode, `03 00` disables; live capture verified |
| Fire strobe | Same subcommand, payload `01` for zero argument, otherwise `02 duration` | Static only; not exercised |

Next work should improve exposure/color quality and turn the proven transport
into a maintained ReRead backend with one owner, firmware loading at
boot/resume as needed, cleanup on sleep, and shared capture access for apps.
Keep image conversion/encoding on established libraries. A V4L2-facing adapter
can be considered if an application needs it; a kernel driver is not necessary
to prove capture.

## Evidence and limits

Primary binary: `opt/colbert/lib/libcolbert-camera.so`, SHA-256
`b8cf21e44aad76ea7874cd7812a65a916ff0be20b06c65cd7cd9b596b5262b6b`.
RAM firmware: `usr/share/usb/CameraCtlr.hex`, SHA-256
`61d003fca740a8ea4901c73f6a8fc2a07fbb3d1d8b203231e51fda1138a9b23e`.

REA/Hopper evidence retained locally in `analysis/inventory/camera-protocol-rea.json`,
`camera-stream-rea.json`, `camera-framing-rea.json`, `camera-autofocus-rea.json`
and `camera-color-rea.json`:

| Question | Address | Evidence ID |
| --- | --- | --- |
| Command framing | `f1b0` | `ev_39c2a22f0e2d9636f060555b592b2a32f99b25a1f67c60b6242d73ed8014e0fe` |
| Bulk command write | `dcf0` | `ev_2c20f2b421bbf07c20d5f16bc3148b60ee5a7f52cd0209aaf267bfa84fe2d32b` |
| Image read | `d670` | `ev_64f30411fc39cd56e75b832e0c0337c4d0dafd2a1ce1ca3cf1e9707529085306` |
| Controller version | `118e0` | `ev_0e36dab8b2d2170098de9065004501f8df591bb756c9a228401ad8d696cdab3e` |
| FPGA register read | `10310` | `ev_3036d91cb59d590816934c7a2d6758fe9681443886025bcbfc554c1f1dc26dc7` |
| I2C read | `106f0` | `ev_76b2b34ced2b31e4ec6c8e7819264d0d4259a6d02652ae165201fdd980755379` |
| Start setup | `12770` | `ev_9ce6441d42f03bfb14d242d3c2318666387bb3f1f1b3db5b64b1a0024dd58bc4` |
| Enter stream mode | `120f0` | `ev_84c366d1405e33f5ef04cc5b04390306be66f16cd4a292f45f7d8c8d2a601b07` |
| Still capture sequence | `13cc0` | `ev_1a48c7276f6dcb4ddafae904ed267874a93bd028c946b61070ac191509d3cc58` |
| SetState / sleep | `11820` | `ev_bb36045df61e7a12e74a2ee520df08dcbc4096039598a82003f2c7cd794d309e` |
| Sensor defaults | `119e0` | `ev_3da91e736bbffc1bc75b39e7de35949c47377bacfe5802e0ec9c1b1ea9703c2a` |
| Bayer frame preparation | `17520` | `ev_0516f8780c051cf164bf33a19e8dcfe41f4d0b78e6ee76c2e0c15dc63c280dc0` |
| Clear image endpoint | `d5e0` | `ev_2e9c2c685c12134dcb8a9fe7be5ed495df5268841bbb29df235181fe071194d1` |
| Focus loop | `12da0` | `ev_86b685e0adf10c8b0a830af470b6cd5c9ead7a79a3434e99a0cfe74796df1b50` |
| Bayer conversion | `17200` | `ev_2935d9596e7025ba0badb99ac69c53717772f19544c5bf70215cf484bc5d00ae` |

GNU `objdump -dC` supplied the exact x86 call arguments and resolved PLT names,
particularly where Hopper pseudocode omitted arguments or mistook PIC thunks.
The correction to `SetState` is based on the actual `bec4 <sleep@plt>` call.
Static decompilation is approximate and does not itself establish runtime behavior.

Live evidence is in ignored `build/antix/camera-*.log`, `camera-stream.raw` and
`camera-preview.png`; it includes initial USB inventory/descriptors, RAM loader
output, version queries, capture output and final cleanup register readback.
Early failed probes remain distinguishable from later successful capture logs.
Captured images and extracted stock binaries/firmware remain ignored local data.

Checklist: SSH **restored**; controller protocol, stream capture, complete still
capture and frame extraction **verified**; focus-lock path, focus light and flash
**verified in the described live setup**; calibrated exposure/color, optical
focus across distances, recovery behavior and lifecycle integration **unfinished**.
No compatibility execution of the old camera library was needed.


## Live preview and focus follow-up (2026-11-08)

ReRead 0.3.1 adds shared continuous stream preview. Each read matches stock
`CaptureStreamImage` at **0x123cd–0x12403**: scheduler priority 20%, clear endpoint
halt, read three frames in one transfer. Complete marked frames are decoded with
libdc1394's **GBRG=513** filter and Pillow; the still filter remains BGGR=515.

Focused GNU disassembly supplements retained focus Evidence
`ev_86b685e0adf10c8b0a830af470b6cd5c9ead7a79a3434e99a0cfe74796df1b50`:

- **0x131f0**, **0x134d0–0x1357f**: if focus/prelock precedes exposure lock
  (raw register 1000 bit 15 unset), stock writes FPGA **10a=1**, waits 750 ms,
  and polls again. That recovery is now implemented.
- **0x142c0** restores the saved sensor gain after still capture, before
  re-entering stream mode. Live-view capture now preserves this behavior.
- **0x11b34** writes sensor **0c=0** during defaults, previously omitted.
- `dac::Read` **0x11cc0–0x11ce6** requests I2C address **0c**, with no register
  index, then shifts the returned word right five. Live readback changed while
  streaming. This corroborates control activity, not an optical focus result.

The reported blurry-shot values `e3f3/c001` satisfy stock's genuine focus-lock
bit test; changing the label or adding sharpening would not establish a fix.
The earlier implementation reset the sensor on every shot and immediately
entered focus acquisition, unlike the stock interaction where streaming runs
while framing. Reusing a running stream, and letting standalone startup stream
for two seconds before acquisition, yielded visibly sharper ambient stills on
this Reader. This is an observed improvement; the precise cause of the earlier
blur and repeatable focus accuracy on a fixed target remain unresolved.

Artifacts: `build/antix/camera-current.jpg` (earlier blurred still),
`camera-current-warm.jpg`, `camera-current-cold-settled.jpg`, and
`camera-live-browser-check.log`. Browser validation covered changing live frames,
capture/download, rejection of concurrent capture, resume after capture and lease
expiry after leaving the page. It does not validate stock-equivalent image
processing or all focus recovery branches.

## Stock architecture and preview throughput (2026-11-08)

The traced stock capture path is a native **userspace C++ library**, not a
camera-specific kernel capture interface. `StreamServer::Capture` calls
`detail::CaptureStreamImage` at **0x199ab**; that follows the direct libusb bulk
path documented above. Retained REA/Hopper Evidence:
`ev_6f54bfb81b7989147f2d2a88f72a683b7303260129c59f4b0d58d7c2ceca8243`,
`analysis/inventory/camera-stream-server-rea.json`. This is static call-path
evidence, not a runtime trace of the stock application.

The constructor **0x19ef0** adds three assembly-line stations at **0x1a0b2**,
**0x1a129**, and **0x1a1a0**. `AssemblyLine::AddStation` starts a Boost worker
thread at **0x1e084**. `StreamServer::Process` calls `ProcessStreamImage` at
**0x1928c**; the processing path calls raw-frame preparation (**0x18f3a**),
lens-shading correction (**0x18f7d**), Bayer conversion (**0x19018**), and gain
control (**0x19060**), followed by dispatch to the application callback. This
establishes a threaded native capture/processing/delivery pipeline. It does not
establish the stock application's actual displayed frame rate.

On ReRead's physical Reader, ten consecutive stock-sized raw reads returned
1,866,240 bytes each. After the first 0.8082-second startup read, nine reads took
**0.2045–0.2060 seconds each**: approximately **14.6 raw frame equivalents/sec**
in the current sensor mode. This is transfer throughput, not measured usable
preview FPS and not a maximum sensor specification. The 2 FPS web preview must
not be described as a hardware limit.

Current preview reads a three-frame burst, converts one selected frame, then
starts its next read. This discards frames and serializes USB waiting with image
processing. Profiling also exposed per-frame Python LUT creation/rounding:
Pillow's 16-bit `point()` path handles a 65,536-entry table each time. The profiler
adds overhead, so its timings are not uninstrumented frame rates. An affine
8-bit tone-mapping experiment is retained under
`build/antix/camera-image-affine-experiment.py`; it was not installed or released.
At this investigation stage the device and converter remained at 0.3.1; the
subsequent 0.4.0 native replacement is described below.

Recommended next implementation: a compiled userspace backend for continuous
buffered capture, frame validation and image processing, with the shared service
remaining the single hardware owner. Use established libusb and image libraries;
Python remains suitable for web/settings/control. A kernel driver or V4L2 adapter
is not required by this traced stock path. Focus and calibrated image processing
are separate unfinished work; native code alone does not establish their quality.
