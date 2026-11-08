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

## Completion checks still in progress

The final one-command fresh installer build, final optimized local-preview rate,
idle CPU measurement, app removal/reinstallation preservation, and USB-session
process log readback are being completed. A reboot was requested after the USB
controller test; these live checks resume when access returns. Power-button sleep
worked before this UI change, but a fresh S3 test is intentionally deferred while
the owner is away because timer wake is unreliable.
