# ReRead on-device UI

ReRead uses Qt 5 Widgets on the LinuxFB platform plugin. Qt paints straight to
Linux `/dev/fb0`; there is no desktop, X server, Wayland compositor, browser, or
GPU requirement. The Reader exposes a 960 × 544, 32-bit framebuffer with a
4096-byte stride. Qt handles that layout. Use large pixel fonts and keep all
controls inside `Screen::content()`: the default 28-pixel margin accounts for
the overlapping bezel. Settings can adjust it from 20 to 60 pixels.

The home screen launches installed applications, browses photos/files, and opens
system settings. The web interface is for file transfers, network provisioning,
packages and device administration. It does not run device applications.

## Controls and ownership

`libreader-ui.so.1` supplies the consistent header, safe content rectangle,
footer hints, selection list, transient notices and physical-button transport.
Apps subclass `ReRead::Screen`, implement `draw` and `button`, then run a normal
Qt event loop. `reader-keys.h` is the stable semantic button enumeration; apps
never depend on historical stock labels or raw scan codes.

Only the foreground process grabs evdev and paints the framebuffer. The launcher
hides and releases its input descriptors before starting an app with QProcess;
on exit or launch failure it returns to Home and grabs input again. Apps exit on
Back at their root or Favorites (Home). The SDK sets Linux's parent-death signal,
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

`reader-brightness` uses Linux backlight sysfs, preferring `psb-bl`. It restores
the saved brightness at boot and wake. `/etc/reader/display.json` holds
`brightness_percent` (10–100), `safe_margin` (20–60) and `idle_dim_seconds`
(0 disables dimming; 120 seconds by default). Idle dimming is transient: it does
not overwrite the user's saved brightness. Any control restores that brightness.
The camera releases its preview lease while dimmed; hardware closes after the
lease expires. Automatic suspend is deliberately not enabled: manual power-button
sleep is available, and unattended timer wake is unreliable on this device.

USB management is started only while the kernel UDC reports `configured`.
`reader-usb-session` listens to Linux netlink carrier changes, with a slow fallback,
and owns web, Dropbear SSH and DHCP child lifetimes. Wi-Fi remains independent.
The camera's native acquisition workers block while unused; a running app must
explicitly request preview frames or a photo to power up streaming.

Qt references: [embedded Linux](https://doc.qt.io/qt-6/embedded-linux.html),
[Linux device input](https://doc.qt.io/qt-6/inputs-linux-device.html). Qt 5 is used
because Debian supplies its maintained i386 packages for this Reader's CPU.
