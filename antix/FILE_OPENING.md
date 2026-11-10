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
`reread-images.desktop` for images, `reread-text.desktop` for plain text/Markdown/CSV/JSON/XML,
and `reread-audio.desktop` for supported audio types. The image handler declares
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

## Supplied handlers and callers

Images uses Qt and distro image plugins for decoding; Text uses Qt's document
layout and Unicode codecs. Audio uses GStreamer playbin with ALSA output and
base/good distro plugins. These are small independent X11 front ends for the
Reader's controls and safe inset, with no viewer service or custom codec.
Images uses `%U`; Text uses `%f`; Audio uses `%u`. Handlers must accept native
paths as well as encoded `file:` URIs: GIO may convert a file URI when launching.

Files dispatches all regular files through GIO. No inline text/image fallback
remains; a missing association shows details and suggests installing a viewer.
Booru opens cached samples with the default image handler. Camera's last-photo
action opens its saved JPEG the same way, and releases its live preview lease
while the viewer has focus. Internet Archive opens cached images/text and sends
audio URLs to the default **audio MIME handler**, rather than the HTTPS scheme's
browser. Its catalog selection remains underneath the new window.

For remote audio, first infer the MIME type from the filename with
`g_content_type_guess()`, get its URI-capable handler using
`g_app_info_get_default_for_type()`, and call `g_app_info_launch_uris()`. The
handler then owns streaming. The default Audio player supports HTTP(S); Images
and Text require local files. Caches remain caller-owned. Thumbnails and browsing
previews are separate from opening files and stay in their respective apps.

References: [freedesktop MIME defaults](https://specifications.freedesktop.org/mime-apps/latest/default.html),
[desktop Exec field codes](https://specifications.freedesktop.org/desktop-entry-spec/latest/exec-variables.html),
[GIO AppInfo](https://docs.gtk.org/gio/iface.AppInfo.html).
