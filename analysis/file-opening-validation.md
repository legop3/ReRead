# Shared file opening: 0.19 validation

ReRead 0.19.0 supplies Text 0.1.0 and Audio 0.1.0 alongside Images 0.1.0.
Files 0.5.0, Camera 0.7.0 and Internet Archive 0.8.0 use normal GIO MIME
handlers. Booru already adopted image dispatch in 0.3.0. Apps remain independent
X11 programs; no file-opening daemon or custom viewer protocol was added.

## Checks on 2026-11-10

- Full core/default-app package build and separate Archive package build passed.
  Desktop entries validate and source diff whitespace checks pass.
- Text tests ran offscreen in the builder and on the Reader: encoded paths,
  literal markup, Unicode/BOM decoding, wrapping, scrolling, zoom anchoring,
  help overlay press/release, unchanged source bytes, bounded large previews,
  missing files and binary rejection. A framebuffer capture confirmed readable
  text and the fixed screen inset on the real display.
- GStreamer playback tests ran in both environments with a clocked fake sink:
  WAV, MP3, Ogg Vorbis and FLAC decoding, duration, actual position, pause,
  seeking, sleep notification, error handling and resource release on close.
  A range-capable local HTTP fixture checked streaming and seeking. An additional
  end-of-media test checked resource release and replay.
- Real ALSA output ran on the Reader. The user confirmed clear speaker playback.
  login1 ListInhibitors showed an idle inhibitor during playback and none after
  pause. Distro GStreamer runtime dependencies added about 72 MB; they introduce
  no idle background service. No power-consumption comparison was performed.
- Installed MIME queries selected Text for plain text/JSON, Audio for MP3/WAV/
  Vorbis/FLAC, and Images for JPEG. Files opened Text and Audio through GIO, and
  Back returned to the exact same Files X window, then Home. Test files stayed
  under /tmp; shared user files were not altered.
- Camera's existing nested Pictures/Camera JPEG opened in Images. Closing Images
  returned to the same Camera window. Live preview yields its lease while
  unfocused: native stream running became 0, then became 1 with fresh frames
  after returning. No new photograph or flash capture was needed for these checks.
- Archive's isolated real-GIO tests covered cached images/text, encoded remote
  audio URLs, preserved catalog selection, no download on cache hit, cache
  lifetime after caller exit and eviction. They made no public Archive requests.
  Public catalog/authentication/stream availability was not revalidated here.

The default viewers remain replaceable with normal mimeapps.list preferences.
Text is currently a read-only, labelled 2 MiB preview; markup is source text.
PDF, EPUB and video viewers remain future work. New installer source includes
all three handlers and their dependencies; the published USB image remains
0.17.0 and can be updated in place through APT. UI guideline changes are deferred.
