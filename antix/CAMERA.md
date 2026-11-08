# Shared camera interface

ReRead uses a native C++ camera engine. It owns USB capture, focus and
exposure sequencing, flash, frame alignment, image processing and JPEG encoding.
The Python service provides HTTP/JSON, firmware loading and file/job management.
The on-device Camera app and CLI share this service, supervised by antiX/runit.

Open **Camera** on the Reader's Home screen. OK/Select or Shoot takes a photo;
Options changes flash, Photo/Document rendering and orientation. View toggles
the last saved photo. Preview and photos default to 180° rotation for the
bottom-facing camera pointed forward while holding the Reader away from you.
Back returns Home. The USB web interface is reserved for management and file
transfer; it does not operate the camera.

Photos have unique filenames under `/var/lib/reader/files`. Settings and files
survive package updates. The service keeps only the latest thumbnail/metadata
under `/var/lib/reader/camera`; live frames remain in RAM and temporary capture
files are removed. It reports the actual stock-cropped output dimensions:
**2588×1916** stills and **646×478** live images.

## CLI and apps

```sh
reader-camera status
reader-camera capture
reader-camera capture --document
reader-camera capture --flash
```

Captures wait for focus, transfer and encoding, then print JSON with the file
name, dimensions, mode, flash choice and focus telemetry. A hardware lock flag
is telemetry, not a guarantee of optical sharpness. Errors return nonzero.

Python apps can use the small installed client:

```python
import sys
sys.path.insert(0, '/usr/local/sbin')
import reader_camera
job = reader_camera.request('capture', flash=False, mode='photo')
status = reader_camera.request()
```

Poll `status['job']`, matching the returned ID. States are `running`, `complete`
and `failed`; only one capture may run at once. Mode is `photo` or `document`.
Document rendering reproduces stock's reduced-saturation still path; it is not
OCR or a grayscale conversion. The same API is HTTP over the root-owned socket
`/run/reader-camera.sock`:

```sh
curl --unix-socket /run/reader-camera.sock http://localhost/status
curl --unix-socket /run/reader-camera.sock -H 'Content-Type: application/json' \
  -d '{"flash":false,"mode":"document"}' http://localhost/capture
```

GET `/status` returns state/error, paused, job, latest, engine=`native`, live,
and native stream counters/rates. POST `/capture` returns 202, or 409 when
busy/paused. A completed job includes `photo.filename`, dimensions, size, time
and `focus` fields (raw/status/gain/prelocked).

GET `/live?after=<sequence>` waits up to ten seconds for a new JPEG and returns
its `X-Frame` sequence header. GET `/rgb?after=<sequence>` provides packed RGB888
for local native apps, with `X-Width`, `X-Height` and `X-Frame`. This bypasses JPEG
encoding and decoding for the on-device preview. Clients must check the exact
`width * height * 3` body size and own their returned buffer. Each preview request
renews an eight-second hardware lease. The engine only encodes live JPEGs when
JPEG clients actually request them. Closing Camera, viewing the last photo, or
idle dimming stops renewing the lease. Saved-photo downloads use management's
`/files/<filename>` route.

POST `/capture` additionally accepts `rotation: 0` or `180` (default 0 for API
compatibility). Rotation is applied during native encoding, after stock's row
and channel normalization, to both the full photo and saved thumbnail. Metadata
records it; the Camera app displays the corresponding live rotation.

## Native implementation and lifecycle

`camera_hardware.cpp` implements the recovered volatile USB/FPGA protocol,
per-unit SMBIOS focus calibration, stock focus recovery/prelock behavior, sensor
mode changes, gain restoration and flash cleanup. The camera's own FPGA runs
the lens/exposure loop. It never programs EEPROM or persistent FPGA storage.

`camera_native.cpp` separates the hardware and processing workers. Four queued
libusb transfers keep reads outstanding; frame markers are reconstructed across
transfer boundaries. Two queued complete frames bound RAM use, and stale preview
frames are dropped when processing falls behind. A still takes ownership between
stream transfers, then live capture resumes. JPEG delivery cannot block USB.

`camera_processing.cpp` reconstructs stock lens correction, white balance,
demosaicing, sharpening, histogram gain and document saturation. Five offline
comparisons produced byte-identical processed buffers to executed stock-library
functions. Still JPEG rows/channels are normalized at the encoding boundary to
match stock's file-writing path; live output retains stock's RGB order.
Encoding uses ordinary Debian libjpeg-turbo; USB uses ordinary libusb. Original
Intel libraries, Boost and OpenCV are used only by optional research comparisons,
not installed with or required by ReRead.

Boot/resume loads the small FX2 RAM firmware if needed. Idle leaves the camera
light off. Sleep rejects new captures, lets the current capture finish, cancels
and drains queued USB transfers, joins native workers and releases the camera.
Resume creates a fresh owner and repairs camera-only USB synchronization if
needed. Package updates use that same cleanup before restarting the service.
Logs are in `/var/log/reader/camera.log`.

## Validation and boundaries

See [processing evidence and repeatable oracle](../analysis/camera-native-processing.md)
and [protocol/hardware findings](../analysis/camera-protocol.md). The unmodified
stock library was also executed on the Reader: stream, autofocus and a complete
9,953,280-byte still succeeded. A fresh native page capture was visually compared
with that stock reference, independently of focus-lock flags. A red object and a
blue object were photographed to check saved-image channel order.

Complete frames are required. ReRead rejects missing/corrupt rows instead of
recreating stock's damaged-frame row-repair heuristics. Degenerate zero-color
white-balance input has a finite fallback. Rare focus recovery branches have
been recovered and reviewed; the fixed-page hardware comparison does not test
every lighting condition, distance or failure state. JPEG compression and small
preview scaling are ReRead choices; byte-exact claims concern the recovered
pixel-processing buffer, not JPEG file bytes.

## Build input

Provide `build/firmware/CameraCtlr.hex` from your Reader's
`/usr/share/usb/CameraCtlr.hex` or its stock backup. SHA-256:

```text
61d003fca740a8ea4901c73f6a8fc2a07fbb3d1d8b203231e51fda1138a9b23e
```

This owner-supplied firmware stays ignored by Git and is included in generated
platform packages for offline boot/resume. The full stock backup and original
libraries are optional research inputs. `release.py` builds the native engine
with the same package definition used by `make-usb.sh`; there is no second OS or
Python capture-engine fallback. The builder supplies i386 C++/libusb/libjpeg
headers; the Reader needs only their ordinary runtime libraries.
