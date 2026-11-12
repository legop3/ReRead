# Making ReRead apps

**This is the main development guide and design contract for ReRead applications.**
Start here for UI, controls, icons, files, lifecycle, building, packaging and
updates. The linked subsystem documents provide implementation details.

ReRead apps are independent Linux programs, distributed as Debian packages.
antiX, Xorg/Matchbox, Qt, GIO, ALSA, elogind and APT provide the underlying
facilities. Use existing Linux interfaces and libraries before adding custom
infrastructure. For new ReRead interfaces, **use the shared ReRead UI and device
helpers** so navigation, layout, physical icons and system state stay consistent.
These helpers are libraries, not an app server or plugin framework. An existing
program or a task requiring another toolkit can integrate without them, but must
follow the same interaction, storage and lifecycle rules.

## Contents

- [Start with a working example](#start-with-a-working-example)
- [Design with the shared UI](#design-with-the-shared-ui)
- [Buttons and their icons](#buttons-and-their-icons)
- [Application identity and icons](#application-identity-and-icons)
- [Files, private state and caches](#files-private-state-and-caches)
- [Web configuration for text entry](#web-configuration-for-text-entry)
- [Open files in the default application](#open-files-in-the-default-application)
- [System services and lifecycle](#system-services-and-lifecycle)
- [Build, package and distribute](#build-package-and-distribute)
- [Before publishing](#before-publishing)

## Start with a working example

Copy [screen-window.cpp](antix/apps/examples/screen-window.cpp) for a new native
UI. It demonstrates `ReRead::Screen`, a collection/details view, palette roles,
semantic button events, the Guide overlay, focus cleanup and ordinary app exit.
[package-screen.py](antix/apps/examples/package-screen.py) makes an installable
example `.deb`, including its desktop entry, app icon and Software metadata.
The example is a developer tool; it is not part of default installs or releases.

An app-only development build needs Docker and this source checkout. It does
**not** need the stock image, camera firmware, an installer build or a USB drive:

```sh
# Run from the repository root.
docker build -t reread-app-builder image-builder
docker run --rm -v "$PWD:/work" reread-app-builder sh -c '
  antix/ui/build.sh build/antix/ui &&
  antix/apps/examples/build.sh build/antix/ui &&
  python3 antix/apps/examples/package-screen.py
'
```

Result: `out/examples/reread-app-example_0.2.0_i386.deb`. Upload it through the
web Apps page or copy it to the Reader and run:

```sh
reader-update install /path/to/reread-app-example_0.2.0_i386.deb
```

For subsequent example edits, reuse the builder and existing SDK output; rerun
only the examples build and packager. There is no reason to rebuild the OS.
For a real app, use its own directory under `antix/apps/<id>/`, executable,
`VERSION`, icon, build script and packager. Keep its source in that directory.
[Text](antix/apps/text/) is a compact document-handler example;
[Files](antix/apps/files/) and [Software](antix/apps/software/) show collections;
[Camera](antix/apps/camera/) shows asynchronous service integration.


## Design with the shared UI

The display is logically **960 × 544**, with a fixed **14-pixel safe inset on all
four sides**. No keyboard, mouse or touch input is required for normal operation.
Use `Screen::content()` for content and `safeArea()` for custom overlays; do not
reimplement bezel margins or make them adjustable.

Include `reader-ui.hpp`, subclass `ReRead::Screen`, implement `draw(QPainter &,
const QRect &)` and `button(reader_button, int)`, then run a normal Qt event loop.
The shared helper handles focused X11 keys, semantic button mapping, system
notices, Hold state, safe bounds and optional chrome. Windows remain normal X11
windows: do not hide Home, watch its PID, switch virtual terminals, write the
framebuffer or grab raw input to perform an app handoff.

Use the shared drawing functions:

| UI need | Shared API |
| --- | --- |
| Collection with selection details | `listInfo`, `listInfoRow` |
| Short action menu or confirmation | `list` |
| Consistent typography and elision | `label`, `text` |
| Physical control glyph | `buttonIcon` |
| Brief feedback | `toast` |
| Contextual control hints | `controls`; Screen owns Guide and overlay visibility |

Collections should normally put the selectable rows on the left and useful
information about the selected item on the right. Keep folder/package metadata,
artwork and app-specific actions in the app; the library supplies layout and
selection drawing. If a missing UI pattern is useful to several apps, add it to
the shared library instead of maintaining copied drawing code. Keep app-specific
models and behavior in the app. Use simple menus for a few actions, rather than stretching
every screen into a collection. Preserve selection and scroll position when
returning from another screen or another app.

Use the shared `QPalette`: pure black background, readable white text, strong
selection contrast and saturated accents with a purpose. In particular, use
`HighlightedText` on a selected `Highlight` row. Do not introduce a separate
low-contrast theme, pastel panels, gradients, glows or idle animations. Use clear
labels and comfortably readable type; existing apps generally use 22–28 px for
body text. The app owns its layout; there is no mandatory app-name/status header.
Set `showHeader=false` when a header adds no useful information.

For inverted use, `orientation=180` rotates Screen's painted UI and reverses its
D-pad directions. Image/camera pixel rotation and saved-file orientation are
separate decisions. A launch context may set `REREAD_SCREEN_ORIENTATION` to `0`
or `180` (core 0.21+) so a shared Screen viewer follows the caller's grip without
changing the global X display. Set it with `g_app_launch_context_setenv`, never
by changing Home's environment or an X session configuration. Programs using
other toolkits may ignore this optional hint. Camera deliberately applies an additional preview rotation
for its inverted grip; use its implementation as the reference for that case.

### Help is a hold-to-show overlay

Every interactive screen must provide useful contextual controls with the
physical **button icons**, each paired with a short action label.

- Help is hidden during normal use and consumes no layout space.
- Holding Guide (the question-mark button) shows the help bar over the content.
- Releasing Guide hides it immediately; repeat events must not toggle it.
- Clear help when the window loses activation, hides or becomes inactive for
  sleep/dimming, so a missed key release cannot leave the overlay latched.
- Keep the controls list current when entering menus, confirmation dialogs or
  another mode. Guide is reserved exclusively for this purpose; do not bind another action.

**SDK behavior (0.20 / UI 5):** populate `controls` for each screen. Screen
handles Guide press/release, repeat suppression and cleanup on focus loss,
Hold lock, hiding, dimming and sleep. Help is a raised child overlay, above
ordinary child widgets too, and never changes `content()`. Long hint lists wrap
at readable size; inverted screens rotate and reposition help with their UI.
The overlay also includes shared battery/charging, Wi-Fi, volume, free storage
and time, using the same presentation as Home. It always works, including screens
with no app-specific hints. Its Close app hint is supplied automatically.
Do not handle Guide or Favorites in an app or add a permanent footer. `helpVisible()` exposes
the state for diagnostics; it is not a visibility toggle.

## Buttons and their icons

[buttons.json](antix/input/buttons.json) is the canonical input map;
[the physical reference](antix/input/README.md#physical-reference) gives locations,
Linux codes, XKB names and icon names. Use `READER_*` IDs from `reader-keys.h`
through Screen, or focused X11/toolkit key events in another toolkit. Do not copy
scan codes into apps or create an app-specific key map for this hardware.

| Control | Usual app behavior |
| --- | --- |
| D-pad | Move selection; pan or scroll content when appropriate |
| OK | Activate the selection or primary contextual action |
| Back | Return within the app; close from its root |
| Favorites | Always close the app, exposing its caller or Home; Home itself returns to its main screen |
| Options | Context actions or settings |
| Guide | Hold for help; release to hide |
| Play | Play/pause or another clearly labelled primary action |
| Zoom − / + | Zoom or adjust a meaningful quantity |
| View | Change the view |
| Shoot | Capture in apps that use the camera |
| Voice Speed | Speed or another clearly labelled app-specific action |
| Power, volume, Hold | System-owned; do not bind competing app handlers |

Event values are **0 = released, 1 = pressed, 2 = repeated**. Allow repeat for
navigation/scrolling, and use only the first press for opening, deleting,
launching another program or other one-shot actions. This avoids one physical
press opening several nested folders or starting multiple viewers. Screen owns Guide press/release; another toolkit must provide both. Screen consumes Favorites and calls the normal Qt `close()` path; honor
`closeEvent` and clean up playback, subprocesses and inhibitors there or in the
app destructor. It is not a game-exit, cancel or back action. Put those actions
in the app menu. Existing redundant handlers are ignored by Screen. Home's
internal `reread-home` property supplies its sole launcher exception.
Close normally with a successful exit; the window manager
restores the previous window without a custom return-to-Home protocol.

### Application icons and physical-button icons are different

An **application icon** identifies a program on Home and in Software. A
**button icon** identifies the physical control that performs an action in the
current context. Do not draw the word "OK" or use an emoji instead of the
physical glyph, and do not redraw the glyph in each app.

Button icons are shared, transparent SVG theme assets installed under
`/usr/share/icons/hicolor/scalable/actions/`. Use:

```cpp
buttonIcon(painter, rectangle, READER_BACK);  // Screen helper
// Or, from an ordinary Qt widget:
QIcon icon = QIcon::fromTheme("reread-button-back");
// reader_button_icon_name(READER_BACK) supplies that same theme name.
```

Most glyphs are white on transparency; Play retains its blue pill and white
triangle. Preserve that color in normal icon rendering. Monochrome glyphs may
be tinted for a light selection or background. Labels identify actions; glyphs
identify physical controls. A button's old stock-OS name does not force an unrelated action in every app.

The shared artwork must match the Reader's physical markings:

| Button | Required shape |
| --- | --- |
| OK | Empty circle, distinct from the record-like Favorites glyph |
| Play | Blue diagonal pill with a white northeast-pointing triangle near its upper end |
| Back | Thick northwest arrow with rounded ends |
| Options | Long top bar with two shorter right-aligned bars below; no enclosing box |
| Voice Speed | Thick downward arrow with speed lines on its tail |
| D-pad | Arrow points/chevrons without tails |

These corrections are implemented in the shared SVGs and generated reference.
Keep icon names and semantic IDs stable. To change shared artwork, edit the SVGs
and descriptions in `buttons.json`, then run `python3 antix/input/generate.py`
and its `--check` mode. Never ship per-app replacements for physical controls.

## Application identity and icons

Choose a stable lowercase ID, such as `example`. Package it as
`reread-app-example`, install its executable under `/usr/lib/reread/apps/example/`,
and provide `/usr/share/applications/reread-example.desktop`:

```ini
[Desktop Entry]
Type=Application
Name=Example
Comment=A useful description for Home
Exec=/usr/lib/reread/apps/example/example
TryExec=/usr/lib/reread/apps/example/example
Icon=reread-example
Categories=Utility;
X-ReRead=true
Terminal=false
```

Home discovers `X-ReRead=true` entries through GIO. Standard `Hidden`,
`NoDisplay`, `TryExec`, translations, `OnlyShowIn`/`NotShowIn`, working directories
and Exec field codes apply; the desktop identity is `ReRead`. Home starts launcher
apps without file arguments. GIO owns Exec expansion and D-Bus activation; no
shell is implicitly added. Set an application/desktop name in your toolkit too.

Every app with a UI must provide an app icon, including document handlers.
Use a unique `Icon=` theme name, with SVG artwork in
`/usr/share/icons/hicolor/scalable/apps/reread-example.svg` or appropriately sized
PNG theme directories. Do not put an extension in a theme icon name. Home shows
roughly 100-pixel icons, so prefer a clear silhouette with limited detail. Keep
artwork visible against black. Absolute icon paths are supported, but normal
hicolor names are preferred. The installer checks visible launcher entries for
an `Icon=` value; validation of actual artwork remains the app author's job.

Document-only handlers use `NoDisplay=true` to stay off Home while retaining
MIME associations and a Software entry. Background-only packages need no launcher
entry or icon. One primary desktop entry per launcher app is the normal pattern;
additional document-handler entries may be appropriate.

## Files, private state and caches

The shared user-files root is **`/var/lib/reader/files`**, also exported as
`REREAD_FILES_DIR`. Read it from the environment when available. Use ordinary
filesystem operations and create meaningful subfolders as needed, such as
`Pictures/MyApp/`, `Documents/`, `Music/` or `Downloads/`. This is the exact folder
used by Files and the web interface (Copyparty `/files/`); it shares free space with the OS.

| Kind of data | Location / ownership |
| --- | --- |
| User documents and media | `REREAD_FILES_DIR` and its subfolders |
| Private persistent app data | Namespace below user-wide `XDG_DATA_HOME`; Qt `AppDataLocation` with your application name |
| Existing apps' private data | Launcher-provided `READER_APP_DATA_DIR=/var/lib/reader/apps/<desktop-basename>` |
| Disposable downloads/thumbnails | Namespaced XDG cache / Qt `CacheLocation`, with a size limit |
| Editable system defaults | `/etc/reread/apps/`, declared as Debian conffiles |
| Executables and shipped assets | `/usr/lib/reread/apps/<id>/` and `/usr/share/reread/apps/<id>/` |

`XDG_DATA_HOME=/var/lib/reader/apps` is **per user, not per app**. Set your toolkit's
application name before asking it for app-specific paths. Do not change XDG roots
when launching another program. `READER_APP_ID` and `READER_APP_DATA_DIR` are
legacy caller-specific variables; clear them in the launch context for another
application, as shown below.

Use atomic writes for settings/download completion, avoid overwriting existing
user files, and handle files being moved or deleted by another app or the web
interface. Bound cache size and memory use: the Reader has about 512 MB RAM and
a 4 GB SSD. Removal and upgrades must retain user documents and private data;
never ship or wipe runtime data in a package maintainer script. See the full
[storage policy](antix/STORAGE.md).

The web storage hub also exposes app data, XDG configuration and bounded caches
for inspection and maintenance. These are direct filesystem views, so apps must
handle external changes. Packaged code/assets are browsable there as read-only
volumes; APT owns their updates. See [the volume map](antix/STORAGE.md#copyparty-storage-hub).

## Web configuration for text entry

**Keep settings that can reasonably be changed with Reader buttons in the app's
Reader UI.** The web editor is for inputs that need a keyboard: API keys, account
names, server URLs, saved searches and other text or structured entries. Do not
move ordinary switches, playback/display options or button-friendly choices to
the web just because a form can render them. For example, Booru's automatic image
opening and server selection are Reader controls; account keys and search text
are web configuration.

Since ReRead **0.21.0**, an app can expose configuration without its own web
server, web page or settings daemon:

1. Ship `/etc/reread/apps/<id>.json` with usable defaults. Declare it in
   `DEBIAN/conffiles`; the app reads this ordinary JSON file directly.
2. Ship `/usr/share/reread/apps/<id>/settings.schema.json`, a **JSON Schema
   draft 7** object describing that file. Use readable `title` and `description`
   fields, limits and defaults; use `format: "password"` for API keys. References
   must be local (`#...`) so editing works offline.
3. Set `"settings": true` in the existing package metadata at
   `/usr/share/reread/apps/reread-app-<id>.json`. The ID is the package name
   without `reread-app-`; paths are conventional, not arbitrary manifest paths.
4. Declare `Depends: reread (>= 0.21.0)` when relying on this web feature. Explain
   when changes take effect; the current generic UI tells users to reopen the app.

Web **Apps → installed app → Settings** discovers installed packages through the
normal manager metadata. The platform bundles upstream **JSON Editor** to render
schema forms in the computer's browser, with no CDN. The backend validates with
the distro `python3-jsonschema`, rejects stale saves and atomically replaces the
JSON file. There is no extra settings database, background polling or per-app
registration. API keys are stored as configuration, not encrypted secrets.

`GET /api/apps/settings?package=reread-app-<id>` returns `schema`, `value`, `path`
and `revision`; `POST /api/apps/settings` accepts `package`, `value`, `revision`.
Apps normally need neither endpoint: read their file at startup and keep normal
button-configurable preferences in private app state. Upgrades retain edited
conffiles; app removal retains them unless the user explicitly purges the package.
Booru's [schema](antix/apps/booru/settings.schema.json) and
[packager](antix/apps/booru/package.py) are the complete working example, including
one-time migration from its older shared-Files configuration.

## Open files in the default application

Use **GIO, MIME types, desktop entries and mimeapps.list**. Files, Booru,
Internet Archive and Camera review all use this path. Reuse the system's viewer
instead of embedding another image viewer, text reader or audio player in your
browser/downloader. Browsing thumbnails and camera live preview remain local.

For a local file in a Qt app (include GIO before Qt headers):

```cpp
#include <gio/gio.h>
#include <QUrl>

// Inside your open action; report failure in the app's UI.
auto context = g_app_launch_context_new();
g_app_launch_context_unsetenv(context, "READER_APP_ID");
g_app_launch_context_unsetenv(context, "READER_APP_DATA_DIR");
auto uri = QUrl::fromLocalFile(filename).toEncoded();
GError *error = nullptr;
bool opened = g_app_info_launch_default_for_uri(uri.constData(), context, &error);
g_object_unref(context);
if (!opened) {
    // Show error->message, or a suitable missing-viewer message.
}
if (error) g_error_free(error);
```

Link `gio-2.0`. From a terminal inside the X session, use `gio open FILE`.
Do not interpolate filenames into a shell command. Keep your window and state;
closing the launched viewer exposes it again. A successful launch does not
prove decoding succeeded: the viewer reports read/format errors itself. Keep
cache files available while a launched viewer may need them.

For remote media, use the **media MIME type**, not merely the HTTPS URL scheme.
Archive looks up a URI-capable handler with `g_content_type_guess()` and
`g_app_info_get_default_for_type()`, then calls `g_app_info_launch_uris()`.
Otherwise an audio URL could launch a web browser. The bundled Audio handler
streams HTTP(S); Images and Text require the caller to download a local file.
See [the complete opening contract](antix/FILE_OPENING.md) and Archive's code.

To provide a file handler, advertise only supported MIME types in its desktop
entry and choose an appropriate Exec field code:

| Exec field code | Handler accepts |
| --- | --- |
| `%f` / `%F` | One / several local filenames |
| `%u` / `%U` | One / several URLs; local documents may also arrive as native paths |

For example, `Exec=/usr/lib/reread/apps/example/example -- %u`. Parse arguments
without a shell, honor `--`, and accept encoded `file:` URIs and native paths when
using URL field codes. Validate remote schemes and report unreadable/unsupported
content. The usual `desktop-file-utils` package triggers refresh associations.
Platform defaults are `/etc/xdg/reread-mimeapps.list`; user preferences override
these. An ordinary app package must not overwrite that shared platform file or
force itself as the default in maintainer scripts. Use `xdg-mime default ...` for
an explicit user preference. Images, Text and Audio are replaceable handlers.

## System services and lifecycle

Apps run as trusted **root** processes. There is no app permission system to
implement. Services already owning hardware remain responsible for it.

| Facility | Use this interface |
| --- | --- |
| Display/input | Qt Widgets/X11 and Matchbox; shared Screen/Device helpers |
| Power, auto-suspend, inhibitors | elogind `org.freedesktop.login1` on the system D-Bus |
| Battery and AC state | UPower system D-Bus; optional `/run/reader-battery.json` snapshot |
| Audio playback | Existing Linux audio libraries, e.g. GStreamer with ALSA; use the default Audio app for opening media |
| Master volume | System triggerhappy/ALSA; `reader-audio` for explicit system settings |
| Brightness | Linux backlight via `reader-brightness`; do not create a private dimming policy |
| Camera, focus, flash | Shared HTTP/JSON service on `/run/reader-camera.sock` |
| Packages | APT/dpkg and `reader-update`; no separate app package registry |
| Network | System networking; normal sockets/QtNetwork with asynchronous requests |

### Focus, inactivity and sleep

Screen handles its Device activation. With a different toolkit/widget, mark a
Device active only while foreground and deactivate it when hidden/backgrounded.
Use Qt application activation and widget visibility signals for that connection;
other toolkits can use their equivalent focused-window events. Device does not open raw evdev devices. Its
callbacks are `onIdle`, `onHold`, `onNotice` and `onResume`; `activity()` reports
additional user input that the system's physical keyboard listener cannot see.

Screen's `idleChanged(true)` also occurs when it becomes inactive or sleeps.
Stop expensive preview/rendering/network activity that is only useful on screen;
resume it when appropriate. Playback/background work may intentionally continue
while dimmed or unfocused, so use the actual login1 sleep signal where needed.
Watch `PrepareForSleep(true/false)` for suspend/resume. Stop or pause hardware
work before sleep and reacquire it afterward; CLI suspend returning is not wake.

Playback or an active download can hold a standard **idle** inhibitor, preserving
manual Power sleep. Call login1 `Inhibit`, retain its returned file descriptor,
and close it on pause, completion, error or cancellation. Alternatively:

```sh
elogind-inhibit --what=idle --mode=block --who='Example' --why='Playback' player FILE
```

Use a time-limited **sleep delay** inhibitor when preparation must finish before
suspend. Do not permanently block sleep or implement another suspend timer.
Power belongs to elogind, volume to triggerhappy/ALSA, and Hold to the session
service. Hold reports pressed = unlocked, released = locked; the shared session
owns that interpretation and X input suppression. Do not infer switch position
from toolkit focus/suspend release events.

Use event-driven I/O, socket notifiers and signals. Keep network/decoding off the
UI thread when it can block, bound queues/transfers and stop periodic work when
it has no purpose. Do not poll system state continuously or add a daemon just to
support an ordinary foreground app.

### Camera and optional background services

Use the [camera API](antix/CAMERA.md) rather than opening its USB device.
`GET /rgb?after=<sequence>` supplies native RGB previews; `/live` supplies JPEG.
Requests renew an eight-second lease, so stop them when no preview is needed.
`POST /capture` starts an asynchronous capture; poll `/status` only while waiting
for that job. Returned photo names are relative to shared storage and can include
folders. Use the default image viewer for saved photos. The camera service owns
hardware sequencing, flash cleanup and sleep preparation.

A true background feature may ship a runit service at
`/etc/sv/reread-app-<id>/run`. Its service name must match the package name.
Optional manager metadata is `/usr/share/reread/apps/reread-app-<id>.json`:

```json
{"name":"Example","description":"Purpose","desktop":"reread-example.desktop","service":"reread-app-example"}
```

Omit `service` for foreground-only apps; `desktop` is optional for background-only
packages. Version always comes from dpkg. Do not install an enabled runsvdir link:
Software/web management owns Enable/Disable. Removal must stop app-owned workers
without taking down system services. See [manager.py](antix/manager.py).

## Build, package and distribute

Use the existing builder, target **i386**, and declare actual runtime dependencies.
The current baseline for new apps is **ReRead 0.20.0**. Device SONAME is 1 and UI
SONAME is 5; UI 5 was introduced in 0.20.0. Using a future SDK API requires the
corresponding minimum core version. Do not use the obsolete 0.9 minimum from
earlier documentation for an X11 app.

The core ships public headers, libraries and `reread-ui.pc`/`reread-device.pc`.
Against an installed SDK or correctly configured i386 sysroot:

```sh
g++ -m32 -std=c++17 -O2 -fPIC main.cpp \
  $(pkg-config --cflags --libs reread-ui gio-2.0) -o example
```

`reread-ui` brings the device helper and Qt Widgets dependencies. In a source
checkout, use explicit source include and build-library paths like the example's
[build script](antix/apps/examples/build.sh). Point pkg-config at **i386** packages,
not the host's amd64 libraries. Development headers/toolchains stay in the builder,
not on the Reader. Apps using only Device can link `reread-device`; another toolkit
may need neither library. SDK ABI changes require a new SONAME and rebuilt apps.

Use an ordinary Debian package layout:

```text
DEBIAN/control
usr/lib/reread/apps/example/example
usr/share/applications/reread-example.desktop
usr/share/icons/hicolor/scalable/apps/reread-example.svg
usr/share/reread/apps/reread-app-example.json     # optional manager metadata
usr/share/reread/apps/example/                   # optional packaged assets
```

For compiled code use `Architecture: i386`; use `all` only for genuinely
architecture-independent packages. Name it `reread-app-<id>`, give it a Debian
version, declare `Depends: reread (>= 0.20.0)` plus its other runtime libraries,
and supply a meaningful description. Put editable defaults in `DEBIAN/conffiles`.
Make executables executable. Build with `dpkg-deb --root-owner-group --build`;
our packagers use gzip compression for the Reader. The starter packager provides
a complete working layout. There is no need for custom install-time registration.

Install/update through on-device Software, the web Apps page, APT or
`reader-update install /path/to/package.deb`. Remove through Software or
`reader-update remove reread-app-example`. Updates replace packaged code and
assets while retaining data. Avoid custom updaters or downloading executables
from the application itself. Source/app packaging is independent of installer
creation; adding an optional app does not require rebuilding the USB image.

For publication, use the shared [release tool](RELEASING.md):

```sh
./release.py app images              # Build a local bundle; no publishing
./release.py app images --publish    # Build, push, publish and verify APT
```

Replace `images` with the app directory name. Bump its `VERSION`, validate and
commit source changes before publishing. GitHub release creation, immutable
package checks, upload verification, latest-release policy and APT verification
are shared; do not add per-app publishing scripts or GitHub credentials.
Each app's `package.py` owns only its specific payload/build recipe. Every app
uses the same `antix/apps/package.py` build entry point. Optional apps do not rebuild the installer.

Only maintainers publish to the official archive; other developers can distribute
`.deb` files or their own APT repository. New app names need no Pages rules.

## Before publishing

Verify the behaviors your app actually adds, using disposable data:

- Build/package for i386; validate the desktop entry and installed icon.
- Navigate every screen using only physical controls. One press triggers one
  action; Guide shows only while held, without a layout jump or stale overlay.
- Check the fixed safe inset, readable contrast and visible selection.
- Open a real file via its MIME handler, including spaces/Unicode in its name;
  close the viewer and verify the caller's folder/selection is preserved.
- Exercise failures/cancellation, missing or externally moved files and
  unavailable networks where relevant; keep resource use bounded.
- Verify app-specific focus/sleep cleanup and resource release. Coordinate a
  hardware suspend check with someone able to press Power to wake the Reader.
- Update/remove the package without deleting documents, settings or private data.

Compilation and offscreen checks are useful; they do not establish hardware,
optical focus, audible playback or successful sleep/wake. Scale checks to the
change and record what was actually validated. Documentation-only changes do
not require an OS rebuild, device reinstall or a new APT package.

### Further references

[Shared UI internals](antix/ui/README.md) · [Physical input map](antix/input/README.md) ·
[File-opening details](antix/FILE_OPENING.md) · [Storage](antix/STORAGE.md) ·
[Camera](antix/CAMERA.md) · [APT publishing](repository/README.md)

Linux specifications: [desktop Exec field codes](https://specifications.freedesktop.org/desktop-entry/latest/exec-variables.html),
[MIME defaults](https://specifications.freedesktop.org/mime-apps/latest/default.html),
[icon themes](https://specifications.freedesktop.org/icon-theme/latest/).
