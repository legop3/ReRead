# ReRead on-device UI

Home and Camera use Qt 5 Widgets on the LinuxFB platform plugin. Independent
apps choose their own UI; neither the Screen base class nor these libraries are
mandatory. See [the program contract](../apps/README.md). Qt paints straight to
Linux `/dev/fb0`; there is no desktop, X server, Wayland compositor, browser, or
GPU requirement. The Reader exposes a 960 × 544, 32-bit framebuffer with a
4096-byte stride. Qt handles that layout. Use large pixel fonts and keep all
controls inside `Screen::content()`: a fixed 14-pixel inset on all four sides accounts for the overlapping bezel.
It is not a user preference.

The home screen launches installed applications, browses photos/files, and opens
system settings. The web interface is for file transfers, network provisioning,
packages and device administration. It does not run device applications.

## Controls and ownership

`libreader-device.so.1` is an optional QtCore-only input/power adapter. It owns no
renderer or app lifecycle. `libreader-ui.so.3` supplies optional drawing helpers:
a safe content rectangle, selection list, notices and physical-button icons. Apps choose whether to
show a compact header (`showHeader`), grouped icon/action controls (`controls`
and `showControls`), or overlay controls on their own content (`controlsOverlay`).
There is no mandatory header, subtitle row or footer bar. Camera uses a full
preview with small overlays; Home and Settings use the compact system header.
Set `orientation=180` to rotate the entire screen, including overlays, and reverse
the D-pad directions to follow the rotated display. Saved camera JPEG rotation
is separate from screen orientation.
Apps choosing Screen subclass it, implement `draw` and `button`, then run a
normal Qt event loop. Other apps may use ordinary widgets plus Device, or their
own Linux input/rendering without either library. The Camera service client
belongs to the Camera executable, not the shared drawing library. `reader-keys.h` is the stable semantic button enumeration; apps
never depend on historical stock labels or raw scan codes.

Only the foreground process grabs evdev and paints the framebuffer. The launcher
hides and releases its input descriptors before starting an app with QProcess;
on exit or launch failure it returns to Home and grabs input again. Apps exit on
Back at their root or Favorites (Home). The launcher sets Linux's parent-death signal before exec,
so an app does not remain orphaned after its launcher is terminated. No shell is
involved in app execution. External USB arrow/Enter/Escape keys also work.

Power press/release suspends through `reader-power`; hardware long press remains
the emergency power-off. Volume keys use ALSA via `reader-audio` in every app.
Hold events lock ordinary controls (pressed means unlocked, released locked).
The initial Hold switch state before its first event is not yet established.
Guide supplies contextual help; Options opens an app's settings. UI work happens
on input, service replies and infrequent status updates, not a constant animation
loop. Apps should release busy resources when hidden or idle.

## App packaging and SDK

See [the app contract](../apps/README.md). Native apps build against ordinary Qt
and `pkg-config --cflags --libs reread-ui`; the core installs the public headers,
shared library and pkg-config file. Install Qt development headers in a separate
build environment, not on the handheld. SDK ABI changes require a new library
SONAME and rebuilt app packages; source-level button IDs stay stable.

Build all UI binaries with `antix/ui/build.sh`, inside the image builder, or call
`antix/release.py` to build both the core and Camera app packages. The normal
one-command USB build includes both. Apps can be updated separately afterward.

## Display and idle policy

`reader-brightness` uses Linux backlight sysfs, preferring `reader-panel`. It restores
the saved brightness at boot and wake. `/etc/reader/display.json` holds
`brightness_percent` (10–100) and `idle_dim_seconds`
(0 disables dimming; 120 seconds by default). Idle dimming is transient: it does
not overwrite the user's saved brightness. Any control restores that brightness.
The camera releases its preview lease while dimmed; hardware closes after the
lease expires. Automatic suspend is deliberately not enabled: manual power-button
sleep is available, and unattended timer wake is unreliable on this device.

USB management requires an active USB Ethernet carrier as well as a configured
gadget. The legacy UDC can leave its configured state stale after disconnect.
`reader-usb-session` listens to Linux netlink carrier changes, with a slow fallback,
and owns web, Dropbear SSH and DHCP child lifetimes. Wi-Fi remains independent.
The camera's native acquisition workers block while unused; a running app must
explicitly request preview frames or a photo to power up streaming.

Qt references: [embedded Linux](https://doc.qt.io/qt-6/embedded-linux.html),
[Linux device input](https://doc.qt.io/qt-6/inputs-linux-device.html). Qt 5 is used
because Debian supplies its maintained i386 packages for this Reader's CPU.

The 0.7.0 split uses device SONAME 1 and optional UI SONAME 3. Upgrade core and
Camera 0.3.0 together when moving from the earlier combined helper. The core package declares a conflict
with the old Camera binary instead of letting it start with an incompatible
SDK. Future apps should declare the minimum core version for their SDK.
