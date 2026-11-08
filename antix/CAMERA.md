# Shared camera interface

ReRead 0.3.1 packages the camera service, USB backend and FX2 RAM firmware.
`reader-camera` is supervised by runit and is independent of the web server.
Apps share this service rather than opening the USB device themselves. One photo
can be in progress at a time; another capture is rejected while it is busy.

The Camera page is at `http://192.168.7.1/#camera`. Choose whether to use flash,
frame the subject in the live view, take a photo, and download the full-resolution
JPEG. Live view uses the camera's actual 648×480 stream (about two displayed
frames/second on the development Reader), rather than repeatedly taking stills.
Pause it with the live-view button; leaving the page or hiding the browser stops
renewing its eight-second lease. Multiple viewers share one hardware stream.
Each photo gets a unique filename in `/var/lib/reader/files`, alongside other user files. Those files
survive platform updates. The service keeps only the newest small preview and
metadata in `/var/lib/reader/camera`; raw temporary frames are removed.

## Apps and SSH

```sh
reader-camera status
reader-camera capture          # Waits for autofocus, capture and JPEG encoding
reader-camera capture --flash
```

The CLI prints JSON including the saved photo filename. Capture errors have a
nonzero exit status. A CLI wait timeout does not cancel an already-running shot.

Local Python apps can load the installed client and call:

```python
import sys
sys.path.insert(0, '/usr/local/sbin')
import reader_camera
job = reader_camera.request('capture', flash=False)
state = reader_camera.request()  # Poll state['job'] until complete or failed.
```

Use the returned job ID to match the result. The service reports the newest job;
an app should collect its result before starting another. The same interface is
ordinary HTTP/JSON over the root-owned Unix socket `/run/reader-camera.sock`:

```sh
curl --unix-socket /run/reader-camera.sock http://localhost/status
curl --unix-socket /run/reader-camera.sock -H 'Content-Type: application/json' \
    -d '{"flash":false}' http://localhost/capture
```

HTTP POST `/capture` returns 202 with a job, or 409 when busy/paused. GET `/status`
returns `state`, `error`, `paused`, `job` and `latest`. Job states are `running`,
`complete` and `failed`. A completed job includes `photo.filename`, dimensions,
size and capture time. The existing USB web server exposes GET `/api/camera`,
POST `/api/camera/capture`, and GET `/api/camera/preview` for its camera page.
Downloads use the existing `/files/<filename>` route.

GET `/live?after=<frame-number>` on the Unix socket returns one JPEG and its
`X-Frame` sequence header, waiting up to ten seconds for a newer frame. Each
request renews an eight-second live-view lease. The web equivalent is
`/api/camera/live?after=<frame-number>`. Status includes `live` (lease requested)
and `frame` (latest sequence). During a still capture the last preview can remain
visible; new live frames resume afterward. Live frames stay in RAM and are not
saved in Files.

## Lifecycle and implementation

Startup loads firmware into the camera's RAM when needed and leaves the camera
idle with the focus light off. Live view initializes the sensor once and keeps
autofocus/exposure running between bursts. A still borrows that running camera,
then restores the stock sensor gain and stream mode; a standalone capture first
drains stream frames for two seconds so initialization is not mistaken for a
settled exposure/focus state. Sleep stops live view, pauses new captures and waits
for any current capture to clean up. Resume rediscovers the camera, reloads RAM firmware
if necessary, and repairs endpoint synchronization with a camera-only reset if
needed. Package updates pause the camera and restart its runit service. Saved
photos are never package-owned files.

The hardware-specific code is `camera_hardware.py`, recovered from the stock
USB/FPGA protocol; see [research evidence](../analysis/camera-protocol.md).
Autofocus uses each Reader's SMBIOS focus calibration, not a hardcoded offset.
Image conversion uses Debian's `libdc1394-25` Bayer decoder and Pillow's image
operations/JPEG encoder. No FireWire device is used: libdc1394 supplies only a
[standalone Bayer conversion function](https://sources.debian.org/src/libdc1394-22/2.2.3-1/dc1394/conversions.h/).
The conversion path added about 8 MB of dependency packages on the development
Reader; a full-resolution conversion took about 2.4 seconds there.

Color/exposure processing remains basic, with visible color cast and noise.
Focus lock is a hardware status indication, not proof that the image is sharp.
The stock exposure-before-focus refocus branch is implemented; its prelock and
USB-reinitialization fallback paths remain unfinished. Do not treat camera
support as equivalent to the stock application's final image quality. Short
transfers or damaged line markers produce an error instead of publishing a corrupt photo. The service does not automatically
repeat a failed flash shot. Logs are in `/var/log/reader/camera.log`.

Verified on the development Reader: update from 0.2.1 to 0.3.0 through the web
updater with files/settings retained; browser capture, preview and JPEG download;
rejection of a second simultaneous capture; rejection while paused; real S3 sleep
through `reader-power` with RTC wake; and a successful flash capture through the
CLI after resume. The converter also rejected a deliberately removed interior
row marker in an otherwise full-length raw frame.
Fresh 0.3.0 SSD and USB installer images were built and checked for the final
camera files. Physical testing used the in-place update path; this work did not
erase/reinstall the Reader.

## Live-view validation (0.3.1)

On the physical Reader, a browser displayed changing 648×480 frames, saved a
2592×1920 photo while previewing, and resumed live view afterward. Leaving the
Camera page expired the live-view lease. A standalone capture after a two-second
stream settling period also completed. The new ambient stills had visibly
sharper scene edges than the earlier severely blurred shot; a controlled
printed-page comparison is still pending, so this is not a measured focus fix.
The raw DAC readback changed during streaming, corroborating lens-control
activity, without establishing physical lens position or focus accuracy.
Pausing the camera stopped streaming and left LED register 17 at zero and mode
register 12 at one; live JPEGs were retrieved again after resume.

## Firmware build input

Supply `build/firmware/CameraCtlr.hex`, the 23 KB controller firmware from your
Reader's `/usr/share/usb/CameraCtlr.hex` (or an extraction of that file from its
stock backup). Expected SHA-256:

```text
61d003fca740a8ea4901c73f6a8fc2a07fbb3d1d8b203231e51fda1138a9b23e
```

This owner-supplied file stays ignored by Git. It is included in the generated
platform package at `/lib/firmware/reread/CameraCtlr.hex`, so fresh installs and
updates work offline without access to the stock backup. The stock OS and old
Intel application are not runtime/build dependencies. No EEPROM repair firmware
or persistent FPGA updater is included. The normal `make-usb.sh` and release
builder consume this same firmware input and platform package definition.
