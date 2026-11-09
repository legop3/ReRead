# ReRead application contract

Apps are ordinary Debian packages named `reread-app-<id>`. The USB management Apps
page installs, updates and removes `.deb` files using apt/dpkg. Versions and
dependencies come from dpkg; there is no separate package format or registry.
Removal keeps user data and settings. Platform updates preserve installed apps.

## Foreground applications

Install a standard XDG `.desktop` entry in `/usr/share/applications/`:

```ini
[Desktop Entry]
Type=Application
Name=My application
Comment=A short purpose shown on Home
Exec=/usr/lib/reread/apps/example/example
Icon=example
Categories=Utility;
X-ReRead=true
Terminal=false
```

ReRead discovers entries marked `X-ReRead=true`, using Name and Comment on Home.
Hidden/NoDisplay entries are excluded. Exec launches an executable with arguments,
without a shell. File/URL field codes are not passed: the current launcher starts
apps without a document. Use one entry per foreground application. Its desktop
basename is the stable UI identity, e.g. `reread-camera`.

Every foreground app **must supply an icon** through the standard `Icon=` key.
Install its SVG or PNG in the normal hicolor theme, for example
`/usr/share/icons/hicolor/scalable/apps/example.svg` with `Icon=example` (no
extension). Use a unique application name for the icon. An absolute image path
is also supported. Prefer a clear silhouette with few details: Home displays
icons at about 100 pixels. No ReRead-specific image format or drawing API is
needed. Home resolves theme names with Qt's icon-theme support and shows a
fallback for missing/broken artwork so the application stays accessible.
The app installer rejects foreground desktop entries with an empty/missing
`Icon=` before installing; background-only services do not need icons.
See the [desktop-entry icon key](https://specifications.freedesktop.org/desktop-entry/latest/recognized-keys.html)
and [Linux icon-theme specification](https://specifications.freedesktop.org/icon-theme/).

Native packages use `Architecture: i386` and declare their real dependencies.
Apps choosing the new device helper need `Depends: reread (>= 0.7.0)`; apps
using neither helper can declare the earliest platform version they require. Code belongs in `/usr/lib/reread/apps/<id>/`, shared
assets in `/usr/share/reread/apps/<id>/`. The launcher exports `READER_APP_ID`,
`READER_APP_DATA_DIR=/var/lib/reader/apps/<desktop-basename>` and `XDG_DATA_HOME`
pointing to that private data location. Apps create their own files there. Public
photos/documents intended for transfer belong in `/var/lib/reader/files/`.
Never ship runtime data in a package or delete it in removal scripts. Editable
system defaults belong in `/etc/reread/apps/` and `DEBIAN/conffiles`.

Apps are trusted local Linux processes, currently running with the handheld UI's
root privileges. This is not an app sandbox. They must not replace platform
services, touch raw hardware already owned by a service, or reset global settings.

## Independent programs and lifecycle

An app is its own Linux executable, launched as a child process. No ReRead base
class, plugin API, shared UI, theme or runtime library is required. Choose your
own toolkit, widgets, layout and rendering. Home only launches the desktop
entry, releases its framebuffer/input, and returns when the program exits.
It also sets Linux's parent-death signal before exec, so the main app terminates
if Home dies even when the app links no ReRead library. Foreground programs must
not daemonize; clean up their own worker processes on exit.

There are two **optional** helpers for Qt applications:

- `reread-device` / `reader-device.hpp`: a QtCore-only `ReRead::Device` object
  that handles calibrated evdev input, Hold, volume, sleep/wake and idle dimming.
  It owns no window and draws nothing. Register callbacks with `onButton`,
  `onIdle`, `onHold`, `onNotice` and `onResume`; call `setActive(true)` only while
  owning foreground input and `setActive(false)` when handing it back. Call
  `activity()` for any additional input your own widgets receive.
  It also applies the user's optional auto-suspend timeout; activity restarts
  both idle timers. Auto-suspend uses the existing Linux sleep helper and defers
  while a package operation holds the package lock.
- `reread-ui` / `reader-ui.hpp`: optional Qt Widgets drawing helpers and a
  `Screen` widget, used by Home and Camera by choice. Lists, button glyphs,
  margins, headers, notices and rotation are conveniences, not an app format.

Use normal `pkg-config --cflags --libs reread-device Qt5Widgets` to build a Qt
Widgets app that chooses device helpers, or `reread-ui` when choosing the drawing
helpers too. The supplied builder targets i386 with `-m32`. SDK files are shipped
in the core package; headers/libraries can be copied into a development sysroot
without installing the Reader OS on a workstation.

[device-window.cpp](examples/device-window.cpp) is an ordinary QWidget/QVBoxLayout
application with its own appearance, using only `Device`. It has no `Screen`
subclass and does not link `libreader-ui`. After building the libraries with
`antix/ui/build.sh`, `antix/apps/examples/build.sh` builds this developer example
inside the builder. It is not installed as a default app.

When choosing `Device`, disable duplicate toolkit evdev handling before creating
QApplication with `qputenv("QT_QPA_FB_DISABLE_INPUT", "1")`; the example and Camera
do this. Home does not force that setting on independent applications. Apps using
their own toolkit input or raw Linux evdev can ignore both optional libraries.
They use the same button IDs/keycode table in `reader-keys.h` and existing system
helpers below; handle system controls and release input/resources on exit.

Button values follow Linux evdev: 0 released, 1 pressed and 2 repeated. Ignore
release for ordinary navigation; use value 1 for actions that must fire only once.
IDs are semantic enums from `reader-keys.h`. Up/Down move focus, OK activates,
Back returns within the app then exits from its root, Favorites exits to Home,
Options opens app settings, and Guide explains controls. Provide useful grouped button-icon/action hints when they help. Power, volume and Hold belong to the shared system layer. Avoid idle
animations or polling; use Qt socket notifiers, signals and timers only as needed.
Use Device's `onIdle` callback, or Screen's `idleChanged(bool)` when choosing
that widget, to stop/restart expensive activity during dimming.

Only one foreground app owns the framebuffer/input. The launcher releases them
before starting an app and returns automatically when it exits, including crashes.
Stop activity and close local-service requests on exit. Launch failures return to
Home with feedback. The launcher terminates the main app on parent death rather than leaving input
grabs behind. An app must not start a second framebuffer renderer.

## Platform services

Audio uses ALSA (`default:CARD=MID`); master volume uses `reader-audio`.
Display brightness uses `reader-brightness`; power uses `reader-power`.
Apps using their own input handling call `reader-audio up/down` for volume and
`reader-power suspend` after a short Power release. Hold value 1 means unlocked,
0 means locked; suppress ordinary keys while locked. These are Linux command
interfaces, usable from any language, not requirements to link a UI library.
Battery state is `/run/reader-battery.json`, sampled by the existing Linux
power-supply monitor. UI services do not emulate these Linux subsystems.

Camera is a single-owner native service with HTTP/JSON over
`/run/reader-camera.sock`. GET `/live?after=<sequence>` supplies a JPEG and X-Frame
header. Native local previews use GET `/rgb?after=<sequence>` for packed RGB888
with X-Width, X-Height and X-Frame, avoiding JPEG encoding/decoding. Both renew an
eight-second hardware lease. POST `/capture` accepts
`{"flash":false,"mode":"photo","rotation":180}`; mode can be `document`,
rotation is 0 or 180. Poll GET `/status` only while waiting for that job. Photos
are atomically saved in Files. Stop preview requests when leaving or idle;
streaming powers down after the lease expires. In inverted mode the camera app rotates its entire UI by 180 degrees; the
preview has an additional image rotation and saved JPEG orientation is explicit. Global sleep pauses camera ownership safely.

## Optional background services

An optional `/usr/share/reread/apps/reread-app-<id>.json` supplies manager metadata:

```json
{"name":"My application","description":"A short description","desktop":"reread-example.desktop","service":"reread-app-example"}
```

`desktop` and `service` are optional. Version always comes from dpkg. A background
service's name must equal its package name, with an executable `/etc/sv/<package>/run`.
Do not ship an enabled runsvdir link: management Enable/Disable owns that choice.
Foreground-only apps do not need a service or a metadata file.

Build with `dpkg-deb --root-owner-group --build package-directory`. Install/update
through Apps, or `reader-update install /path/to/app.deb` over SSH. Remove through
Apps, or `reader-update remove reread-app-example`. The bundled Camera is the
first separately packaged foreground app and follows this same contract.
