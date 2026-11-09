# Internet Archive for ReRead

Independent ReRead foreground app. Uses Archive.org public advanced-search,
item metadata, and file endpoints. There is no keyboard, account, or on-device
download management. Supported source formats are streamed from the network.
Favorites save only Archive item identifiers.

## Build a local test package

From the **repository root** on your development computer, with the existing
`reread-builder` Docker image (see `antix/README.md`):

```sh
docker run --rm -v "$PWD:/work:z" reread-builder sh -c '
  /work/antix/ui/build.sh /work/build/antix/ui &&
  python3 /work/antix/apps/archive/package.py
'
```

Output: `out/antix/reread-app-archive_0.1.0_i386.deb`.

This builds *only* the app and its locally required UI SDK libraries. It does
not build the firmware, platform package or installer, and does not publish
anything. It also does not touch any GitHub workflow or APT repository files.

Connect the Reader via USB, open its management web interface at
`http://192.168.7.1`, and upload the output .deb on the **Apps** page.
Or copy the file to the Reader and run
`reader-update install /path/to/reread-app-archive_0.1.0_i386.deb`.
Dependencies (including `ffmpeg`) must be obtainable from configured APT
sources; the Reader needs working internet access for those packages.

## Controls

Up/Down: browse, or scroll text. OK: select an item/file or stop playback.
Back: return to previous screen; on Home returns to ReRead.
Favorites hardware key: return to ReRead Home. System Power, Hold, and Volume
remain managed by ReRead.

Browse Videos, Audio, Books & texts, Images, Software, Live music and Favorites.
Items are discovered by public Archive search, ordered by download count.
The item page shows files, favorite toggle, and description. The file viewer
supports JPEG/PNG/WebP, UTF-8 plain text, and FFmpeg-supported audio/video
streams. Compatible playback is subject to the device's CPU and file codecs.

## Data and limits

No media is saved to disk. Images and text are fetched with hard response size
limits (12 MiB image, 6 MiB text); audio is streamed by FFmpeg to ALSA.
Video uses two FFmpeg streams (audio via ALSA and low-rate video via a JPEG
pipe into the framebuffer), so performance on the Atom Z520 is *unverified*.
Documents such as PDF, EPUB and scanned page images are not yet rendered; use
Archive-provided text derivatives where available. Unsupported types show a
message instead of downloading. In-memory limits protect device RAM.

Favorites are the **only** persistent data, saved as a small JSON file under
the app data directory. Browsing does not write cache files. The app has no
account login; borrow-only or restricted Archive items may not be playable.

This is an initial hardware-test build, not a fully validated media player.
