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
Icon=applications-utilities
Categories=Utility;
X-ReRead=true
Terminal=false
```

ReRead discovers entries marked `X-ReRead=true`, using Name and Comment on Home.
Hidden/NoDisplay entries are excluded. Exec launches an executable with arguments,
without a shell. File/URL field codes are not passed: the current launcher starts
apps without a document. Use one entry per foreground application. Its desktop
basename is the stable UI identity, e.g. `reread-camera`.

Native applications use `Architecture: i386`, `Depends: reread (>= 0.5.0)` and
their real dependencies. Code belongs in `/usr/lib/reread/apps/<id>/`, shared
assets in `/usr/share/reread/apps/<id>/`. The launcher exports `READER_APP_ID`,
`READER_APP_DATA_DIR=/var/lib/reader/apps/<desktop-basename>` and `XDG_DATA_HOME`
pointing to that private data location. Apps create their own files there. Public
photos/documents intended for transfer belong in `/var/lib/reader/files/`.
Never ship runtime data in a package or delete it in removal scripts. Editable
system defaults belong in `/etc/reread/apps/` and `DEBIAN/conffiles`.

Apps are trusted local Linux processes, currently running with the handheld UI's
root privileges. This is not an app sandbox. They must not replace platform
services, touch raw hardware already owned by a service, or reset global settings.

## UI and lifecycle

Use Qt Widgets with the installed `reread-ui` SDK. Subclass `ReRead::Screen` and
implement `draw(QPainter&, const QRect&)` and `button(reader_button, int)`.
The rectangle is already inset for the bezel. Call `text` / `list` or draw normal
Qt content in that rectangle. Set `title`, `subtitle`, `hint` and use `toast` for
short feedback. A plain QApplication event loop is enough; no application server
or desktop session is required. See `antix/ui/camera.cpp` for a complete app.

Build using `g++ -fPIC app.cpp $(pkg-config --cflags --libs reread-ui)` in an i386
build environment (the supplied builder uses `-m32`). The shared SDK keeps input,
brightness recovery, volume, sleep, Hold and status chrome consistent.

Button values are 1 pressed and 2 repeated; normal app actions should use value 1.
IDs are semantic enums from `reader-keys.h`. Up/Down move focus, OK activates,
Back returns within the app then exits from its root, Favorites exits to Home,
Options opens app settings, and Guide explains controls. Provide useful footer
hints. Power, volume and Hold belong to the shared system layer. Avoid idle
animations or polling; use Qt socket notifiers, signals and timers only as needed.
Override `idleChanged(bool)` to stop/restart expensive activity during dimming.

Only one foreground app owns the framebuffer/input. The launcher releases them
before starting an app and returns automatically when it exits, including crashes.
Stop activity and close local-service requests on exit. Launch failures return to
Home with feedback. Parent death terminates SDK apps rather than leaving input
grabs behind. An app must not start a second framebuffer renderer.

## Platform services

Audio uses ALSA (`default:CARD=MID`); master volume uses `reader-audio`.
Display brightness uses `reader-brightness`; power uses `reader-power`.
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
streaming powers down after the lease expires. The camera app displays the same
180° orientation that it saves. Global sleep pauses camera ownership safely.

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
