# Internet Archive for ReRead

Standalone, button-operated Internet Archive browser, with an original visual
Qt interface designed for the Intel Reader (960 x 544).

## Build locally

From the repository root with the existing reread-builder Docker image:

    git pull
    docker run --rm -v "$PWD:/work:z" reread-builder \
      python3 /work/antix/apps/archive/package.py

The result is out/antix/reread-app-archive_0.6.0_i386.deb.
The script builds only the app and its shared Qt libraries. It does not touch
the operating-system package, installer, GitHub releases or APT workflows.
Use ReRead's USB web interface, http://192.168.7.1, Apps, to upload/install
the .deb, or copy it to the device and run reader-update install FILE.deb.

## Navigation

Home shows six graphical tiles: Audio, Books, Images, Software, Collections
and Favorites. Browse results display cover thumbnails, title and creator
in a six-card gallery, with arrow-key navigation. New results load and append automatically as you browse near the end; the
selection and earlier results remain in place. Options switches between popular
and recent ordering.

The item page shows a large cover, title, creator and description, with
actions for files, favorites and details. Collections can be opened to browse
their contents. The file list shows the format, size and type of each file.

Image viewer: truly full-screen image (no header or footer), Zoom In/Out
and arrow-key pan. Back returns to files; Options offers download.
Text reader: distraction-free full-screen paper pages, Up/Down to turn pages,
Zoom In/Out for
font size. Reads plain-text derivatives, not PDF/EPUB/scanned-page images yet.
Audio player: fullscreen media-focused layout, cover art, title, progress and
elapsed time, Play/OK pause,
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

The on-screen footer uses ReRead's physical button drawings, not textual
stand-ins. The app borrows archive.org's restrained charcoal identity, white catalog,
blue item titles and authentic cover art without trying to fit a desktop
website onto the Reader. The header is compact, and the home/categories
and results use the full width for six large tiles/cards with normal-case
labels. Physical button icons stay visible in a compact footer.

## Testing notes

This version is not yet validated on the physical Reader. Public items and
formats vary; restricted works may not be available without authentication.
Metadata, search and thumbnails come from public Internet Archive endpoints.
Large text and image files are capped to protect the Reader's 512 MB RAM.

## Version 0.6.0 display policy

No persistent 'Internet Archive' header on any page. Only navigation/catalog
pages show a compact physical-button hint footer. Image, reading and audio
screens use the whole 960 x 544 framebuffer without persistent header/footer.
Automatic result pagination requests the next 24 items only when the cursor
nears the end of the loaded batch; requests append instead of replacing existing
results. Scrolling back doesn't reload previous pages.

## Version 0.6.0 improvements

- Zoomed images pan in the expected direction: Left moves the viewport left
  through the image, Right moves it right, and likewise for Up/Down. Panning is
  clamped to the actual image edges. The zoom scale now has 13 steps from
  1x through 10x, including fine steps near fit-to-screen.
- The text reader no longer wraps or allocates lines for an entire book at
  once. It lays out only the current screen, remembers page offsets for
  backward navigation, and uses bounded lookahead even for pathological very
  long lines. Changing type size preserves the current text anchor.
- The full-screen audio player now contains visible physical-button glyphs
  for pause/play, ten-second skips, Back and Download, alongside an enlarged
  title, cover artwork, elapsed time and progress bar.
- Item artwork is shown in file rows; small image files receive individual
  in-memory thumbnail previews when available. Thumbnail requests are
  size- and concurrency-limited and never stored on disk.
- Increased card title, file metadata and description text sizes.
- Streamed MP3 only; no FFmpeg or video dependencies. Explicit downloads
  still go to the shared Files directory.

This version is committed for local testing; successful compilation and
actual Reader performance still need verification on the device.
