# Standard file opening

ReRead uses freedesktop MIME types, `.desktop` entries and `mimeapps.list`, with
GLib/GIO dispatch. There is no ReRead file-type registry, viewer daemon, viewer
plugin ABI, or custom handoff protocol. Xorg/Matchbox handles the launched window;
closing it exposes the calling app, which keeps its state.

## Calling programs

Use GIO's `g_app_info_launch_default_for_uri()` (or its async variant). Build with
`pkg-config --cflags --libs gio-2.0`; encode a local filename as a proper `file:`
URI. Qt callers can use `QUrl::fromLocalFile(path).toEncoded()`. Do not build a
shell command from a filename. From a terminal in the X session:

```sh
gio open '/var/lib/reader/files/Pictures/example.jpg'
gio mime image/jpeg
xdg-mime query default image/jpeg
```

`gio` and `xdg-open`/`xdg-mime` are distro tools included by the platform. Session
processes have `DISPLAY`, `XDG_CURRENT_DESKTOP=ReRead` and a user-wide
`XDG_DATA_HOME=/var/lib/reader/apps`. Home still sets `READER_APP_ID` and
`READER_APP_DATA_DIR` for existing apps. When launching a different app, unset
those two legacy caller-specific variables in `GAppLaunchContext`, as Files does.
New handlers should use their own application name and normal XDG/toolkit paths.

Handlers accept files outside shared storage too, including downloaded app
cache files. The caller owns those files and must keep them available while
another process reads them. Download remote content before dispatching to a
local-only viewer; do not delete its file immediately after a successful launch.
GIO success means the process was launched, not that a file was decoded. A
handler shows read/format errors in its own window; a missing association or
launch failure is reported by the caller.

## Defaults and replacement viewers

System policy lives in `/etc/xdg/reread-mimeapps.list`. It selects
`reread-images.desktop` for supported image types. The image handler declares
`MimeType=...`, `Exec=... -- %U` and `Icon=reread-images`. `%U` lets GIO pass one or
more correctly encoded file URIs. `NoDisplay=true` hides this document handler
from the Home grid while retaining its MIME associations and Software entry.
`desktop-file-utils` supplies normal package triggers to refresh the desktop MIME
cache; `shared-mime-info` supplies content-type detection. User preferences take
precedence over these system defaults:

```sh
xdg-mime default another-viewer.desktop image/jpeg
# Equivalently, use ~/.config/mimeapps.list [Default Applications].
```

A replacement desktop entry must advertise the MIME type. Missing/uninstalled
handlers fall through according to the freedesktop specification. Install or
remove viewers with APT; no ReRead-specific registration command is needed.

## This step

Images is a small Qt front end for physical controls, icon hints and the screen's
safe inset. Qt and its distro image plugins do the decoding. Files dispatches
regular files through GIO, retaining its inline text fallback only when no text
handler exists. Missing non-text handlers show file details and an explanation.
Audio/text handlers and conversion of Internet Archive, Booru and Camera's full
review actions are intentionally later steps. Browsing thumbnails are not file
opening and can remain within those applications.

References: [freedesktop MIME defaults](https://specifications.freedesktop.org/mime-apps/latest/default.html),
[desktop Exec field codes](https://specifications.freedesktop.org/desktop-entry-spec/latest/exec-variables.html),
[GIO AppInfo](https://docs.gtk.org/gio/iface.AppInfo.html).
