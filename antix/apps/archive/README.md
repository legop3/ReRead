# Internet Archive for ReRead

Standalone Qt/X11 Internet Archive browser, packaged as `reread-app-archive`.
Its six categories, cover gallery, item details, favorites, automatic result
pagination and explicit downloads are independent of the media viewers.

Arrows navigate, OK opens, Options changes result ordering or downloads the
selected file, Back returns, and Favorites exits. Downloads are confirmed,
streamed to shared `Internet Archive/` storage, and preserve 128 MiB free.
Canceled/failed partial downloads are discarded. Existing files are not replaced.

## Default viewers

Images and plain-text derivatives open in the system's default MIME application;
closing that window reveals the same Archive selection. Image/text downloads use
a 64 MiB LRU cache under `$XDG_CACHE_HOME/reread-archive/files` (normally
`~/.cache/reread-archive/files`). Individual transfers are capped at 14 MiB for
images and 7 MiB for text. The text viewer may show a smaller labelled preview.
Cache files survive Archive exit, so the launched viewer can continue reading.
Browsing covers and file thumbnails stays inside Archive and is held in RAM.

Audio passes its HTTPS URL directly to the default handler for its MIME type,
using GIO AppInfo. This avoids launching a browser for the HTTPS scheme. The
bundled Audio player streams MP3, WAV, Ogg, FLAC and Opus using GStreamer; Archive
has no audio decoder, playback worker, text paginator or full-image viewer.
Restricted works and unavailable network resources can still fail. Other file
types remain downloadable; open downloaded files from Files with any installed
MIME handler. PDF, EPUB and video viewers are not supplied by this release.

Replacing Images, Text or Audio through normal MIME defaults changes Archive's
opener too. See [file opening](../../FILE_OPENING.md).

## Build and checks

In the existing builder: `python3 antix/apps/archive/package.py`. Only this app
and its SDK libraries are built; no installer is rebuilt. Install the resulting
`.deb` through Software, the web interface or APT. Version comes from `VERSION`.
`test-opening.cpp` exercises real GIO dispatch with isolated MIME defaults,
Unicode/punctuation filenames, cached images/text, streamed audio URIs, cache
retention and eviction. It makes no public Archive requests or account changes.
