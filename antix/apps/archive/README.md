# Internet Archive for ReRead

Standalone, button-operated Internet Archive browser, with an original visual
Qt interface designed for the Intel Reader (960 x 544).

## Build locally

From the repository root with the existing reread-builder Docker image:

    git pull
    docker run --rm -v "$PWD:/work:z" reread-builder \
      python3 /work/antix/apps/archive/package.py

The result is out/antix/reread-app-archive_0.3.0_i386.deb.
The script builds only the app and its shared Qt libraries. It does not touch
the operating-system package, installer, GitHub releases or APT workflows.
Use ReRead's USB web interface, http://192.168.7.1, Apps, to upload/install
the .deb, or copy it to the device and run reader-update install FILE.deb.

## Navigation

Home shows six graphical tiles: Audio, Books, Images, Software, Collections
and Favorites. Browse results display cover thumbnails, title and creator
in a six-card gallery, with arrow-key navigation. Play loads the next set of
results. Options switches between popular and recent ordering.

The item page shows a large cover, title, creator and description, with
actions for files, favorites and details. Collections can be opened to browse
their contents. The file list shows the format, size and type of each file.

Image viewer: full-screen fit, Zoom In/Out and arrow-key pan.
Text reader: paper-style pages, Up/Down to turn pages, Zoom In/Out for
font size. Reads plain-text derivatives, not PDF/EPUB/scanned-page images yet.
Audio player: cover art, title, progress and elapsed time, Play/OK pause,
Left/Right ten-second seek, Back to return.

Options on a highlighted file or an open viewer shows a download confirmation.
Downloads are opt-in, stream directly to Files / Internet Archive and preserve
128 MiB of free storage. Interrupted or canceled downloads are discarded.
Names are sanitized and existing files are not overwritten. No media is
saved during ordinary browsing. Images/text/thumbnails have RAM limits.
Only a small favorites JSON file persists automatically.

## Audio runtime

No FFmpeg or video support. MP3 files stream over Qt HTTPS into a
dedicated worker using the small libmpg123 decoder and ReRead's existing ALSA
speaker output. No audio data is saved to the Reader's disk, and memory
buffers are bounded. MP3 playback, pause/resume and approximate ten-second
network seeking are supported. Seek precision varies with the source file
and its bitrate. Unsupported formats remain downloadable, but do not play.

The only extra audio libraries required are libmpg123-0t64 and libasound2t64.
The app does not install or execute the mpg123 command-line player. Existing
FFmpeg libraries from older installations are not removed by an upgrade.

The on-screen footer uses ReRead's physical button drawings, not text labels
standing in for button icons. The remainder of the application keeps its
custom Archive artwork-first gallery, detail, image and reading interfaces.

## Testing notes

This version is not yet validated on the physical Reader. Public items and
formats vary; restricted works may not be available without authentication.
Metadata, search and thumbnails come from public Internet Archive endpoints.
Large text and image files are capped to protect the Reader's 512 MB RAM.
