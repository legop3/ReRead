# Audio bring-up

## Hardware and playback

On the physical Reader, antiX `5.10.256-antix.1-486-smp` loads the normal
`snd_hda_intel` and `snd_hda_codec_idt` drivers. ALSA card ID is `MID`
(HDA Intel MID), with a SigmaTel STAC9205 codec, vendor ID `0x838476a0`.
Its single analog PCM exposes playback and capture. Speaker playback required
no vendor binaries, private GPIO writes or driver porting.

The owner's `evidence/testaudio.wav` is 48 kHz, 16-bit stereo, 147,122 frames
(3.07 seconds). It was copied to `/var/lib/reader/files/testaudio.wav` and
played at **35% mapped master volume**. The owner confirmed it was clearly
audible through the built-in speaker. Direct `plughw:CARD=MID,DEV=0`, ALSA's
`default:CARD=MID` device, and the installed `reader-audio play` command all
completed successfully. The audible confirmation was for the initial speaker
test. PCM mixing between simultaneous applications was not separately tested.

The test media stays local and is not part of source releases or OS packages.

## ReRead audio interface

[antix/audio.py](../antix/audio.py) installs as `reader-audio`. It uses existing
ALSA tools and the stable internal card ID, avoiding numeric card 0 if USB audio
hardware is attached later. Playback uses `default:CARD=MID` to retain ALSA's
normal default PCM handling. There is no new audio server or background player.

```sh
reader-audio status                       # Live JSON status, including jack state
reader-audio play /path/to/file.wav       # Foreground playback using aplay
reader-audio up                           # Increase mapped master volume by ~5 points
reader-audio down                         # Decrease mapped master volume by ~5 points
reader-audio set 35                        # Set 0–100 mapped percent, unmute
reader-audio mute
reader-audio unmute
reader-audio init                         # Restore standard ALSA mixer state
```

Playback is currently the formats supported by `aplay`, including PCM WAV.
MP3 decoding, playlists and a media-player UI are separate application work.
Playback doesn't hold the volume lock, so physical volume buttons work while
an app plays audio. Apps own their foreground playback process and may stop it
with normal process signals.

Master volume uses ALSA `amixer -M` mapped percentages. Steps are approximate
because the actual codec gain levels are discrete. Speaker and Headphone gains
are initialized to 100%; Master controls loudness. Auto-Mute Mode is Enabled
so the codec's jack-routing behavior remains available.

The existing button owner handles Volume Up/Down press and repeat events for
the calibrated built-in keyboard. Repeat changes are limited to roughly five
per second. It calls the helper and leaves all other button mappings unchanged.
`/run/reader-audio-status` supplies the readable Volume line on the diagnostic
screen. CLI `status` reads the actual mixer rather than a stale cached value.

ReRead 0.2.0 stores mixer state with `alsactl` in the normal
`/var/lib/alsa/asound.state`, outside package payloads. Version 0.1.3 used a
custom `/var/lib/reader/audio.json`; it is migrated on first initialization and
removed only after a successful ALSA store. New installs start at 35%, unmuted.
The helper serializes button changes. Startup and normal suspend/resume restore
the ALSA state. Runtime errors go to `/var/log/reader/audio.log`.


## Headphones and capture

ALSA exposes a read-only Front Headphone Jack control and Auto-Mute Mode. It
currently reports unplugged. **Headphone insertion, playback and speaker muting
have not been physically verified.** Driver/mixer controls alone are not proof
of the real jack's behavior.

A four-second 48 kHz S16 stereo capture was made while replaying the known
speaker test. ALSA capture completed, but each channel was essentially flat:
DC mean about -44 PCM counts, AC RMS 0.33 counts and range -45 to 0 (including
startup samples). That does not establish a useful microphone signal.
The codec's Internal Mic Phantom Jack entry is not proof that a microphone is
physically populated. No microphone support is claimed. The local recording
is `/var/lib/reader/files/audio-capture-test.wav`; it was not uploaded elsewhere.

## Validation

- Owner confirmed audible speaker playback.
- Owner confirmed physical volume controls changed the displayed volume. Live
  ALSA readback and the saved file agreed; the resulting preference was 61%.
- Mute state survived an explicit restore. A temporary mixer-only change to
  35% allowed a modest-volume replay through the new helper, then `init`
  restored the owner's saved 61% preference byte-for-byte.
- The helper and native button code compile in the release build. The normal
  package updater upgraded the physical Reader from 0.1.2 to 0.1.3.
- A real deep suspend and timed RTC wake preserved the saved 61% level;
  the normal resume helper restored it successfully. Audible playback after
  wake was not separately confirmed by the owner.
- A physical cold boot has not been performed for audio in this pass; the same
  restoration helper is called by the startup script.

Local logs are `build/antix/audio-{initial,buttons}-live.txt`,
`audio-capture-check.log`, `audio-restore-check.log`, `audio-resume-check.log`,
`audio-update-live.log` and `audio-installed-live.txt`.

ALSA interface references:
[amixer mapped volume](https://manpages.debian.org/unstable/alsa-utils/amixer.1.en.html)
and [ALSA PCM configuration](https://www.alsa-project.org/main/index.php/Asoundrc).
