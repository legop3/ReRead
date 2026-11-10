# ReRead on-device UI

The shared UI is designed as a handheld control panel. Its Qt `QPalette` uses
pure black (`#000000`), white text and saturated icon colors. Home has an open
icon grid, cyan focus corners and a white nameplate for the selected app.
Menus use simple dividing rules; only the selected row has a white background,
with black text and a cyan marker. Button hints retain the physical control
symbols. Status colors describe actual battery state. Application icons remain
ordinary SVG theme assets, and apps can use standard Qt palette roles. There
are no shadows, gradients or glow effects.

Home and the [Camera app](../apps/camera/README.md) use Qt 5 Widgets on the LinuxFB platform plugin. Independent
apps choose their own UI; neither the Screen base class nor these libraries are
mandatory. See [the program contract](../apps/README.md). Qt paints straight to
Linux `/dev/fb0`; there is no desktop, X server, Wayland compositor, browser, or
GPU requirement. The Reader exposes a 960 × 544, 32-bit framebuffer with a
4096-byte stride. Qt handles that layout. Use large pixel fonts and keep all
controls inside `Screen::content()`: a fixed 14-pixel inset on all four sides accounts for the overlapping bezel.
It is not a user preference.

Home uses a three-column icon grid, with six tiles per page and D-pad navigation.
It launches installed applications, browses photos/files, and opens system
settings. The selected tile has a persistent outline and a short description;
there is no idle animation. Settings groups Display, Sound, Power, Connections
and About, and remembers selection when returning from a category.
The web interface is for file transfers, network provisioning,
packages and device administration. It does not run device applications.

## Controls and ownership

`libreader-device.so.1` is an optional QtCore/QtDBus navigation/session adapter. It owns no
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

Only the foreground process paints the framebuffer and handles navigation.
Input devices are shared nonexclusively with the system services; apps must not
use EVIOCGRAB to steal Power/volume events. Home closes its navigation readers
while an app runs. On exit or launch failure it returns and reopens them. Apps
exit on Back at their root or Favorites (Home). GIO handles standard desktop entries and process spawning; the launch callback
sets Linux's parent-death signal. A Linux pidfd lets Qt observe app exit without
polling. Home returns after normal exit, a crash or a launch error. No shell is
added by the launcher. USB arrow/Enter/Escape keys also work.

Udev hwdb applies the model-specific control map. Zoom now uses Linux's actual
Zoom In (418) and Zoom Out (419) codes; ReRead's semantic button IDs are stable.
Elogind owns Power and automatic suspend, and triggerhappy owns global volume
shortcuts through ALSA. Hold events lock ordinary controls (pressed unlocked,
released locked); initial state before the first event is not yet established.
Guide supplies contextual help; Options opens app settings. The helper observes
central dim/Hold/audio state and login1's `PrepareForSleep` signal. There is no
per-app suspend timer or mixer/power-button handler.

## App packaging and SDK

See [the app contract](../apps/README.md). Native apps build against ordinary Qt
and `pkg-config --cflags --libs reread-ui`; the core installs the public headers,
shared library and pkg-config file. Install Qt development headers in a separate
build environment, not on the handheld. SDK ABI changes require a new library
SONAME and rebuilt app packages; source-level button IDs stay stable.

Build Home and the shared helpers with `antix/ui/build.sh`, inside the image
builder. Each app has its own build script under `antix/apps/`.
`antix/release.py` builds the core and all default app packages; the normal
one-command USB build includes them. Apps can be updated separately afterward.

## Display and idle policy

`reader-brightness` uses Linux backlight sysfs, preferring `reader-panel`. It restores
the saved brightness at boot and wake. `/etc/reader/display.json` holds
`brightness_percent` (10–100), `idle_dim_seconds`
(0 disables dimming; 120 seconds by default). Idle dimming is transient: it does
not overwrite the user's saved brightness. Any control restores that brightness.
The camera releases its preview lease while dimmed; hardware closes after the
lease expires. `auto_suspend_seconds` defaults to 0 (Off); Settings / Power offers
Off, 2, 5, 10 or 30 minutes without button activity. It applies on battery and
external power. The PAM-backed `reader-service` starts a tracked seat0 session
using standard setsid/su; `reader-session` is the GUI-free framebuffer adapter.
It monitors evdev activity, updates the console last-input timestamp used by
elogind, and dims the Linux backlight. This is a tty session: elogind only accepts
`SetIdleHint` for X11/Wayland/Mir. On idle-policy changes, the adapter restarts
elogind because version 255 does not re-arm its idle timer on `ReloadConfig`;
registered sessions and active inhibitor descriptors survive that restart.
Elogind's configured `IdleAction`/`IdleActionSec` owns the actual sleep decision
and respects standard inhibitors. The original per-app timers are removed.
Activity tracking continues across app handoff and while an app uses no ReRead
helper. Package jobs take a standard idle inhibitor for their lifetime.

`reader-power suspend` now queues a login1 request. `PrepareForSleep(false)`
marks completion after wake, not the CLI process exiting. Camera preparation
and Reader-specific fan/audio/brightness restoration use the elogind sleep hook.
A failed Camera preparation cancels suspend through elogind's hook cancellation
support. Applications use ordinary inhibitors and lifecycle notifications,
rather than installing hardware hooks. Wake with the physical Power button.

Home uses Qt's standard icon-theme lookup: the small `reread` theme supplies
system artwork and inherits `hicolor` for app icons. Camera owns its SVG in
`/usr/share/icons/hicolor/scalable/apps/`. The Qt SVG plugin handles scalable
artwork; no desktop environment or full external icon theme is required.

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
