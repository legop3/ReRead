# ReRead framebuffer UI validation

2026-11-08, physical Intel Reader, antiX 26.1 i386 / 5.10.256-antix.1-486-smp.

The implementation uses Qt 5 Widgets / LinuxFB, ordinary evdev, Linux backlight
sysfs, Debian packages and XDG desktop entries. No desktop environment is installed.
`antix/ui/README.md` documents the shared UI and `antix/apps/README.md` the app contract.

## Observed on the device

- In-place upgrade from 0.4.0 to 0.5.0, followed by a real reboot: Home appeared
  on the real 960 × 544 framebuffer. Captures honor its 4096-byte stride.
- Wi-Fi configuration, authorized SSH key and SSH host key hashes were unchanged.
- Input was injected through the actual Reader evdev node, using the already
  calibrated Linux keycodes; no UI-only action endpoint was used.
- OK opened the separately packaged Camera executable. Back returned to Home;
  Options opened Settings. Arrow selection and brightness adjustment worked.
- Brightness changed 100 → 90 → 100 through the UI; sysfs readback and displayed
  percentage agreed. Independent panel PWM measurements are in `brightness.md`.
- Camera preview appeared with the requested 180° orientation. OK saved a
  2588 × 1916 JPEG with rotation=180 in metadata. Preview resumed afterward.
- Options enabled flash and a second OK capture produced a legible, correctly
  colored photograph of the printed page/red packaging. The unlit evening shot
  was noisy; flash made the setup usable. Focus flags alone were not treated as
  proof of image sharpness.
- Leaving Camera stopped native streaming after the preview lease expired.
- A temporary ten-second idle timeout dimmed the backlight to 10 and let camera
  streaming stop. Guide restored brightness to 100 and restarted preview. The
  user's 120-second timeout was restored afterward. No kernel sleep was used.
- Independent asymmetric color fixtures passed RGB output, stock BGR/bottom-up
  normalization and normalization plus 180° rotation.
- All five stock processing comparisons remained byte-exact with the production
  Atom compiler flags, including real still, real document and real live frames.

Local evidence is under ignored `build/antix/`: `ui-home.png`, `ui-settings.png`,
`ui-first-capture.log`, `ui-flash-capture.log`, `ui-after-return.log`,
`ui-idle-check.log`, `ui-stock-oracle.log`, and `ui-orientation-check.log`.
Personal camera images are not published.

## Controller reconnect limitation

A kernel `soft_connect` disconnect/reconnect test successfully disconnected USB,
but re-enumeration failed with host descriptor timeouts (`-110`). A Reader reboot
is needed to recover this particular software-controlled controller restart.
Production session gating only observes UDC state and starts/stops userspace
processes; it never operates `soft_connect` or unbinds the controller. Do not use
software disconnect for unattended tests without an independent recovery route.
Physical cable reconnection is a separate behavior and has not been established
by that failed software test.

## Package and build checks

The complete fresh-root build succeeded in the existing Debian builder. Both UI
executables resolve all shared libraries inside that target root. A real upgrade
to core 0.5.1 preserved an edited display conffile. Camera 0.1.1 was removed and
reinstalled through dpkg; its desktop entry disappeared/reappeared and its data
remained unchanged. The temporary verification files/settings were removed before
packing the release. Local log: `build/antix/ui-final-release.log`.

The final UI fixes were applied to that fresh prepared root through the same Debian
packages, then the SSD image and installer were repacked with unchanged kernel,
initramfs and boot UUIDs. The installer manifest records both the core and Camera
packages. This is a UI-only update and does not require a new kernel/initramfs.

Final artifacts were built and inspected:

- `reread_0.5.1_i386.deb` (core) and `reread-app-camera_0.1.1_i386.deb` (app).
- `reread.img.gz` and `reread-installer-usb.img`; the SSD image is exactly
  4,004,388,864 bytes and its ext3 filesystem reports version 0.5.1.
- `manifest.json` records core 0.5.1 and Camera 0.1.1. Library checksums match
  between the compiled SDK, core package staging and installed target root.
- The installer was built without writing any attached USB stick.

Filesystem metadata gives approximately 1,875.5 MiB available before first boot
and user files, of approximately 3,561.6 MiB usable filesystem capacity. This is
an image measurement, not a new on-device fresh-install report.

Docker's builder-image export stalled on this host through both BuildKit and the
legacy builder. The OS/payload build was therefore run using the already configured
`intelreader-validator` container. The maintained Dockerfile includes all required
Qt development dependencies; the normal one-command entry point remains unchanged.
A successful top-level Docker-image rebuild has not been demonstrated in this run.

## Final-image virtual boot check

The maintained optional `antix/test.py` now checks the Qt framebuffer UI instead
of waiting for the retired console monitor's text. The final 0.5.1 SSD image
booted under QEMU with 512 MiB RAM and a virtual USB keyboard, using a disposable
snapshot. `/` mounted as ext3 and `/boot` as FAT. Home appeared, arrow input moved
the selected card, OK opened Camera, and Back returned Home. With no camera
hardware, Camera displayed its unavailable state without preventing return.
Two power-button suspend/wake cycles passed in the VM; this did not suspend the
physical Reader. The test also inspects the rendered selection borders, rather
than relying on old console status strings.

Evidence: `build/antix/ui-vm-check.log` and `ui-vm-*.ppm` screenshots. The Camera
and final resumed Home screenshots were visually inspected. These virtual checks
prove the final image's boot and UI handoff, not real camera capture, actual
battery use or physical Reader wake behavior.

## Live checks still required

A reboot was requested after the controller test. Once SSH returns, install the
final 0.5.1 core and 0.1.1 Camera packages and check the final preview rate, queued
button ownership, review-to-shoot recovery, idle CPU usage and USB-session process
log. The last installed hardware version is the earlier 0.5.0 development build;
final 0.5.1 hardware behavior is not yet claimed as validated.

Power-button sleep worked before this UI change. Its helper and camera cleanup
path are retained; a fresh S3 test is deferred while the owner is away because
timer wake is unreliable.
