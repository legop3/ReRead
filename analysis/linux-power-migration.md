# Linux power and input migration — ReRead 0.9.0

Implemented and tested on the Reader on 2026-11-09. Camera is packaged as 0.4.0.
This supersedes the proposal in [the initial investigation](linux-power-integration.md).

## Ownership

| Responsibility | Implementation |
| --- | --- |
| Short Power press, manual/automatic suspend | Distribution elogind, standard login1 D-Bus |
| Sleep inhibition and lifecycle | login1 inhibitor file descriptors and `PrepareForSleep` |
| Physical scan-to-keycode mapping | Model-specific udev keyboard hwdb |
| Global volume shortcuts | Distribution triggerhappy calling the existing ALSA helper |
| Session registration | Distribution PAM stack, `setsid` and `su`, supervised by runit |
| Foreground navigation | Ordinary nonexclusive evdev; optional QtCore/QtDBus adapter |
| Brightness and suspend mechanics | Existing Linux backlight and kernel S3 support |
| Reader hardware preparation/restoration | One elogind hook for Camera, fan, audio and backlight |

No desktop, X server, Wayland compositor or systemd init was added. Apps remain
independent executables. Power, volume and idle tracking no longer depend on an
application linking ReRead's helpers. Per-app power handlers, sleep timers,
volume handlers, scan-map programming and exclusive input grabs were removed.

## Necessary framebuffer glue

`reader-session` monitors button activity across application handoffs, handles
transient dimming, and publishes optional UI state. Raw evdev bypasses console
input with the keyboard in K_OFF mode. The adapter therefore updates the
controlling console's access timestamp as input arrives. Elogind uses this
standard timestamp to determine tty-session idleness.

Two details were established from upstream code and the hardware:

- Elogind 255 rejects `SetIdleHint` for tty sessions; that API is limited to
  X11/Wayland/Mir. Registering the framebuffer as one of those would misdescribe
  the actual session.
- Its `ReloadConfig` changes `IdleAction` but does not arm/re-arm the idle timer.
  The adapter applies changed idle policy with a normal runit service restart.
  The daemon preserves registered sessions and active inhibitor FIFOs across
  restart; this was tested with a live inhibitor descriptor. Unchanged settings
  do not restart the daemon. Configuration changes defer while sleeping.

Sources: [elogind session idle implementation](https://github.com/elogind/elogind/blob/v255-stable/src/login/logind-session.c),
[reload method](https://github.com/elogind/elogind/blob/v255-stable/src/login/logind-dbus.c),
[idle timer](https://github.com/elogind/elogind/blob/v255-stable/src/login/logind.c).

The daemon may coalesce idle timer checks by up to roughly 30 seconds; automatic
sleep is not an exact countdown. Off remains the default. Package operations
hold a standard idle blocker on the running Reader. An offline image/chroot
without a system bus has no running sleep policy and needs no inhibitor.

Udev now assigns the actual Linux Zoom In/Out codes, 418/419, to scans 0x69/0x6a.
The previous transport codes were 417/418. ReRead's semantic `READER_*` IDs did
not change. Applications using those IDs remain source-compatible. Hold remains
pressed=unlocked, released=locked; its initial state before any event is still
unverified. Power bypasses Hold; the global volume bindings respect Hold.

## Hardware validation

- All 19 calibrated scan mappings read back correctly from the kernel after
  udev application, including the canonical `screenlock` hwdb name for Hold.
- Global volume press/repeat and Hold gating work. Volume also works while an
  ordinary `/bin/sleep` foreground app runs without any ReRead helper.
- Session dimming changes the Linux backlight and input restores the saved level.
- A real package install holds an idle inhibitor; removal releases it.
- A temporary idle inhibitor kept the Reader awake beyond a five-second test
  timeout. Releasing it allowed elogind to initiate automatic suspend.
- Automatic suspend from live Camera entered S3. The owner pressed Power to
  wake and confirmed both screen and preview returned. Hardware logs show
  preparation at 09:57:31 and resume at 09:57:54 local time, with the same boot
  ID. Brightness returned to 100; the native camera stream resumed at about
  15.9 capture frames/s. SSH and management connectivity returned.
- A full reboot started the PAM session, elogind and global hotkeys normally.
  All 19 mappings, volume/Hold and dim/wake passed again after boot.
- A normal core package update through `reader-update` preserved display and
  network settings, hostname, passwords and SSH credentials.
- An injected Power event reached elogind. A temporary failing sleep hook
  cancelled that request, with the kernel log reporting cancellation and the
  Reader remaining awake. The temporary hook was removed.

Temporary test settings and hooks were removed; automatic sleep was restored to
Off. Detailed logs and disposable fixtures are ignored under
`build/antix/power-migration/`. The original package-chroot and VM suites are
separate optional checks; the results above are actual Reader tests.

Existing USB/SSD installer artifacts are not refreshed by building update
packages. The normal full build includes these source changes and dependencies
when producing the next installer.
