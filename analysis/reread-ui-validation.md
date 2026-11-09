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
- Initial brightness checks verified only UI/sysfs changes 100 → 90 → 100.
  The owner later established that this did not dim the physical panel. The
  corrected driver and physical validation are documented below and in
  `brightness.md`; the earlier PWM observation was insufficient.
- Camera preview appeared with the requested 180° orientation. OK saved a
  2588 × 1916 JPEG with rotation=180 in metadata. Preview resumed afterward.
- Options enabled flash and a second OK capture produced a legible, correctly
  colored photograph of the printed page/red packaging. The unlit evening shot
  was noisy; flash made the setup usable. Focus flags alone were not treated as
  proof of image sharpness.
- Leaving Camera stopped native streaming after the preview lease expired.
- A temporary ten-second idle timeout set reported brightness to 10 and let
  camera streaming stop. Optical dimming was not established in this first
  check. Guide restored the reported level to 100 and restarted preview. The
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


## UI feedback revision, 2026-11-08

The owner returned, rebooted and confirmed connectivity. The earlier goal is
paused at their request. Subsequent UI work was separately requested.

ReRead **0.6.0** and Camera **0.2.0** were built and installed in place over
SSH. Settings and the shared idle path now use `reader-panel`, a Linux
backlight driver reproducing the stock EC command; see `brightness.md` for the
correction to the earlier PWM-based claim. The owner confirmed the stock
command visibly changes brightness. Settings button events changed the new
provider from 100 to 70 and back to 100 without a reinstall.

The UI now has a fixed 28-pixel inset on every side, separate physical-button
icon/action groups and app-controlled header/controls. Camera has no reserved
system header or footer bar. Its inverted mode rotates the entire screen and
D-pad directions, with the extra preview rotation applied separately. Review
loads the original latest saved image into a separate buffer and resets zoom;
Home/Favorites exits directly rather than taking a detour through review.
SDK ABI changes use SONAME 2 and matched core/Camera package dependencies.

Actual evdev input tests verified stable last-photo review, return to live view,
whole-screen inversion, capture directly from review, stable review of that
new saved JPEG and Back/Home handoff. The validation photo was
`photo-20261108-214634-c924e4.jpg`, 2588 x 1916, saved with rotation 180.
The user's previous flash/document/orientation preferences were restored.
Native stream counters during this run reported about 15.8 acquisition FPS
and 13.5 processed preview FPS; these are engine counters, not measured LCD
presentation rate. Screenshots of live and inverted UI were visually inspected.
No optical focus or scene-quality claim follows from this arbitrary desk scene.

Evidence stays ignored under `build/antix/ui-feedback-*`: build/install logs,
button-driven check log, original/inverted framebuffer screenshots and idle
checks. The older 0.5.1 installer image was not regenerated as part of this UI
update. The maintained fresh-build path includes the new packages/driver.

The owner's follow-up reduced the fixed safe inset from 28 to **14 pixels on
all four sides**. Source, shared UI library and update package were rebuilt.
The shared library was installed in place and the foreground UI restarted;
this change does not alter the app SDK ABI or require a reinstall.

## Resumed goal: real power and USB checks

On 2026-11-08 the owner resumed the goal and was present to wake the Reader.
Core **0.6.1** and Camera **0.2.1** were installed without erasing data.
An evdev Power press/release from the Camera app invoked the normal shared UI
power path. The kernel entered deep suspend at 23:17:52 and resumed after the
owner's physical Power press at 23:18:32. The boot ID remained
`341a54cf-7388-46cb-bbfa-3b8a0f77e3ba`: this was suspend/resume, not reboot.
The owner confirmed both screen and live preview returned. SSH/web also returned
automatically. Saved brightness 60 was restored; the original 100 was then put
back. Logs and the resumed framebuffer are `build/antix/goal-{before,after}-sleep.log`
and `goal-resumed-frame.png` (ignored).

Physical unplug/replug, with power/Wi-Fi retained, verified management lifetime.
The UDC remained `configured` while USB Ethernet carrier went to 0. Web, Dropbear
and DHCP all stopped; all three restarted with new PIDs after reconnection.
`usb-session.py` now requires carrier as well as UDC state and handles disappearing
sysfs files. No controller soft-disconnect/reset was used. Evidence is
`build/antix/goal-usb-physical.json`.

A 30-second connected idle sample measured Home at 0.232% of one CPU core,
battery monitoring 0.332%, and idle camera service 0.066%. During the unplugged
20.6-second sample, Home and battery each used 0.291%, while the camera service
and USB watcher used 0.000%. The temporary diagnostic observer itself used 12.7%
and is excluded from platform results; it exited after reconnect. These figures
measure process CPU, not watts or battery life. Linux already uses `schedutil`
and exposes C1E/C2/C4/C6 idle states. Approximately 407 MiB of the 485 MiB usable
RAM was available during the connected idle check (includes reclaimable cache).

Camera 0.2.1 also stops preview requests while its settings menu hides the image.
Actual button input followed by status checks confirmed stream running=0 behind
the menu, then running=1 on return. Camera and review retain the same lease-based
shutdown when leaving or dimming. Evidence: `goal-camera-menu-power.log`.

The default embedded Docker builder repeatedly stalled exporting its cached
layer. A dedicated `docker-container` Buildx builder named `reread` successfully
built/exported/loaded the complete tool image. `make-usb.sh` now uses that builder
without changing the host's default builder or restarting other containers.

## Final release and completion audit

`./make-usb.sh --build-only` completed successfully using the maintained entry
point and the dedicated builder. It produced core **0.6.1**, Camera **0.2.1**,
the complete 4,004,388,864-byte SSD image, compressed payload and automatic USB
installer. The manifest root UUID is `7f192474-1770-4f52-b576-9ab4f6f889ea`.
Filesystem metadata gives approximately **1,875.4 MiB available before first
boot**. `debugfs` confirmed version 0.6.1 and the panel module in the packed root.
No physical USB drive was written. Log: `goal-one-command-build.log`.

The final installer was booted in a 512 MiB KVM-backed VM and streamed its payload
from USB storage onto a disposable virtual Reader-sized SSD. It completed, synced
and rebooted automatically. The root partition reached the final SSD sector.
That installed SSD then booted the Qt framebuffer UI: arrow navigation, Camera
launch and Back/Home worked, followed by two successful virtual suspend/resume
cycles. The Camera-unavailable screen (no camera in the VM) and final Home screen
were visually inspected. Log: `goal-final-installer-check.log`; serial logs are
`out/antix/{installer-test,boot-test}.log`.

Those exact release packages were installed on the real Reader, followed by a
normal reboot. It returned over USB with a new boot ID
`6b2e94e5-4ce6-4455-a030-193e6797e3fc`, core 0.6.1, Camera 0.2.1 and the
`reader_backlight` module automatically loaded from disk. The saved 100% level
was restored through `reader-panel`. Installed SDK/app checksums match the final
build outputs. Wi-Fi, SSH access, display preferences and user files survived.
Evidence: `goal-final-release-install.log`, `goal-final-reboot.log`.

| Original requirement | Authoritative evidence |
| --- | --- |
| Working stock-style brightness | Owner-confirmed visible stock EC change; standard Linux panel driver; real boot and suspend restoration |
| Native lightweight handheld UI | Qt Widgets/LinuxFB source/startup; real Reader framebuffer and input; final installed-SSD VM screenshots |
| Bezel-safe layout and later UI feedback | Fixed 14-pixel inset on every side; optional app chrome; grouped button glyphs; complete inverted Camera UI |
| Web limited to management | Management-only web routes; files/Wi-Fi/packages/settings remain; camera UI and operations are native/on-device |
| Linux app/platform contract | Debian packages, XDG entries, SDK SONAME 2, foreground handoff and app data paths; actual core/app upgrade and launch/return |
| Low idle impact | Bounded process CPU measurements; normal schedutil/CPU idle; no idle animation loop |
| Management off without USB host | Physical disconnect: carrier 0, all three management children gone; reconnect restarted all three |
| Camera off when unused | Native lease/condition variables; stream stopped after leaving/dimming and behind settings; resumed on demand |
| Useful first Camera app | Real select-button capture, separate stable review, capture-from-review, whole-UI inversion, full-resolution saved JPEGs and earlier native stock-oracle validation |
| Portable install/update deliverables | Successful one-command build, versioned core/app packages, real in-place update/reboot and final automatic-installer/SSD VM check |

No original requirement remains open. Future layout preferences and new apps can
be handled as subsequent work; this audit does not claim calibrated luminance,
measured battery life or camera availability in the VM.
