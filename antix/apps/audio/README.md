# Audio

An independent Qt/X11 audio player and normal MIME handler, packaged as
`reread-app-audio`. GStreamer `playbin` handles decoding, streaming, buffering,
tags, duration and seeking. Its distro ALSA sink uses the existing Linux audio
configuration; the global volume buttons and mixer remain system-owned.
There is no ReRead audio daemon or decoder, PulseAudio requirement, FFmpeg,
video renderer, or disk download for streamed audio.

MP3, WAV/PCM, Ogg Vorbis, FLAC and Opus use the base/good distro plugins. Unsupported
encodings report an error. Video is disabled. Local files and HTTP(S) URLs are
supported; URLs are passed as data, never shell commands. The desktop entry uses
`%u` and advertises audio MIME types, with `NoDisplay=true` to hide an empty player
from Home. Open audio from Files or an application such as Internet Archive.

- Play or OK toggles pause; Left/Right seeks ten seconds when supported.
- Back or Favorites closes playback and releases the audio device.
- Actual media position, duration and stream buffering come from GStreamer.
- Playing holds an elogind **idle** inhibitor, released on pause, error, completion
  or exit. Explicit Power sleep remains available. `PrepareForSleep(true)` pauses
  playback; Play resumes it after wake. Dimming does not interrupt audio.

The player uses event-driven GstBus integration with Qt. Only the visible time
and progress are refreshed once per second during playback. Plugins/libraries
are loaded by this process; no service runs while the player is closed.

Build after the SDK: `python3 antix/apps/audio/package.py` (requires
`libgstreamer1.0-dev:i386` in the builder). Runtime packages are normal Debian
GStreamer base/good plugins and ALSA output. No development files are installed
on the Reader. Future installer builds include these dependencies and the player.

[GStreamer playbin reference](https://gstreamer.freedesktop.org/documentation/playback/playbin.html)
explains the standard engine used here. See [file opening](../../FILE_OPENING.md)
for replacing the handler through normal MIME defaults.
