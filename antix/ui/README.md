# ReRead on-device UI

For new apps, start with [Making ReRead apps](../../APP_DEVELOPMENT.md). That is
the main design/development contract; this page describes shared UI internals.
Screen implements the Guide overlay policy for every app using UI 5; apps
provide contextual controls without their own visibility or Guide handlers.

The shared UI is designed as a handheld control panel. Its Qt `QPalette` uses
pure black (`#000000`), white text and saturated icon colors. Home has an open
icon grid, cyan focus corners and a white nameplate for the selected app.
Menus use simple dividing rules; only the selected row has a white background,
with black text and a cyan marker. Button hints retain the physical control
symbols. Status colors describe actual battery state. Application icons remain
ordinary SVG theme assets, and apps can use standard Qt palette roles. There
are no shadows, gradients or glow effects.

Home and the [Camera app](../apps/camera/README.md) use Qt 5 Widgets on X11.
Xorg uses the Reader's existing framebuffer through its unaccelerated fbdev driver;
Matchbox maximizes applications and owns focus/stacking. No desktop environment,
compositor or GPU acceleration is required. Independent apps can use any X11
compatible toolkit; Screen and Device are optional helpers. See
[the program contract](../apps/README.md). The logical screen is 960 × 544.
Keep controls inside `Screen::content()`: the fixed 14-pixel inset on all sides
accounts for the overlapping bezel. It is not a user preference.

Home uses a three-column icon grid, with six tiles per page and D-pad navigation.
It launches installed applications, browses photos/files, and opens system
settings. The selected tile has a persistent outline and a short description;
there is no idle animation. Settings groups Display, Sound, Power, Connections
and About, and remembers selection when returning from a category.
The web interface is for file transfers, network provisioning,
packages and device administration. It does not run device applications.

## Controls and ownership

`libreader-device.so.1` is an optional QtCore/QtDBus navigation/session adapter. It owns no
renderer or app lifecycle. `libreader-ui.so.5` supplies shared drawing helpers,
a safe content rectangle, collection layouts, notices and physical-button icons.
Apps choose `showHeader` and supply `controls`; holding Guide shows those controls
as a raised overlay, and releasing it hides them. No footer space is reserved.
Screen also clears help on focus loss, hiding, dimming, sleep and Hold lock.
It intercepts Guide from child widgets, wraps long lists without shrinking text,
and raises the overlay above app content. There is no mandatory app-name header.
Set `orientation=180` to rotate the entire screen, including overlays, and reverse
the D-pad directions to follow the rotated display. Saved camera JPEG rotation
is separate from screen orientation.
Apps choosing Screen subclass it, implement `draw` and `button`, then run a
normal Qt event loop. Other apps may use ordinary widgets plus Device, or their
own Linux input/rendering without either library. The Camera service client
belongs to the Camera executable, not the shared drawing library. `reader-keys.h` is the stable semantic button enumeration; apps
never depend on historical stock labels or raw scan codes.

Xorg delivers keyboard events to the focused window. No GUI app opens evdev or
writes `/dev/fb0`. Home launches ordinary desktop entries through GIO; GLib reaps
its launch processes and the window manager exposes Home when an app closes.
Home does not hide, watch child PIDs or set parent-death signals on applications.
Apps can open other windows/processes normally; their lifetime is independent of
Home. xinit owns the X server lifetime and runit supervises the PAM session.

The canonical physical key map and reusable freedesktop SVG button icons are
[documented here](../input/README.md). Semantic `READER_*` IDs remain stable. UI library version 5 uses ordinary Qt
key press/release handlers and managed windows. All in-repo apps are rebuilt
for this interface; framebuffer-era app binaries are not supported.

Elogind owns Power, suspend policy and inhibitors. Triggerhappy owns global
volume shortcuts through ALSA. The session adapter owns physical Hold state,
including an X input disable/enable on the built-in keyboard. Qt focus/resume
releases cannot lock the Reader. Guide supplies contextual help; Options opens
app settings. Helpers observe central dim/Hold/audio state and login1 lifecycle;
there is no per-app mixer, power policy, raw keyboard listener or suspend timer.

## Collection views

Use `Screen::listInfo` for a collection with selection details. It returns list
and info rectangles, the visible row range and row geometry. `listInfoRow` paints
selection; `label` supplies consistent typography and elision. Files and Software
use these helpers while supplying their own icons, columns and detail content.
Keep `list` for short action menus and confirmations. The helpers add no model, application lifecycle or background service. They are part of
UI SONAME 5; rebuilt apps depend on `reread (>= 0.20.0)`.

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
using standard setsid/su; `reader-session` is the GUI-free X session adapter.
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

The X11 platform uses device SONAME 1 and UI SONAME 5. Install ReRead 0.20.0
and the rebuilt applications together. Package dependencies prevent mixing this
platform with the earlier app binaries. New applications declare the minimum
core version required by their SDK.

Settings / Power / Auto sleep on selects Battery only or Battery and external
power. `auto_suspend_battery_only` defaults to false for compatibility. Battery-only
mode holds an elogind idle inhibitor while UPower reports external power through
the existing battery status adapter. Unplugging starts a fresh idle interval;
manual sleep and the Power button remain available.
