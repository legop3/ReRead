# ReRead Linux interface audit — X11 migration, 2026-11-10

The goal is independent Linux programs on a small handheld, with custom code for
hardware integration and the ReRead experience. This audit is an ownership review,
not a claim that every service has been rewritten or retested.

| Area | Owner / standard interface | ReRead-specific part |
| --- | --- | --- |
| Display and input focus | Xorg fbdev + libinput; Matchbox; Qt xcb | Tested panel configuration, readable UI and physical-button adapter |
| Application launch | freedesktop desktop entries, GLib/GIO | Home discovery, icon grid and per-app data environment |
| App handoff | X11 window focus/stacking | None in Home: no GUI evdev handoff, PID watcher or parent-death rule |
| File associations | freedesktop MIME/desktop-entry mechanisms planned | Default file openers are deliberately the next task, not implemented here |
| Buttons | Linux evdev/udev hwdb → XKB → focused toolkit events | Model-specific mapping, stable semantic names and physical SVG glyphs |
| Hold switch | Raw model key, X input enable/disable via xinput | Inverted key-style switch and wake handling; not a normal EV_SW switch |
| Power and wake locks | elogind login1 suspend, idle policy and inhibitor FDs | UI settings plus battery-only idle inhibitor |
| Idle/backlight | Linux backlight sysfs, elogind tty idle policy | Session bridges physical activity and dim/Hold/status; X does not replace this |
| Battery | UPower and Linux power_supply | Existing UI adapter and investigated gauge presentation quirk |
| Audio and volume | ALSA; triggerhappy global shortcuts | Mixer selection, saved level and UI status |
| Networking | wpa_supplicant, DHCP, Linux net interfaces | Wi-Fi configuration UI; Reader USB controller/gadget integration |
| Packages and repositories | apt/dpkg, signed Debian repository, GitHub Pages | Software UI and APT job reporting/catalog presentation |
| Storage | Normal directories on the root filesystem; file-manager web service | Documented shared files root and app-private paths |
| Camera | libusb, libjpeg-turbo; native shared library and Unix socket | Vendor USB/FPGA protocol, focus, recovered processing and client API; no working V4L2 driver exists in this implementation |
| Cooling | Existing kernel/firmware behavior | One stock-compatible hardware reset; no custom fan policy |

## Changes made for X11

Per-app raw input discovery/readers were removed from Device. Screen uses focused
Qt key press/release handlers. The UI library uses a new SONAME and all
in-repo applications are rebuilt together. Ordinary Qt widgets can
use QShortcut without Screen; the independent example does that. Home delegates
process launching/reaping to GIO and leaves window lifetimes to the window
manager. Both application icons and physical control glyphs use normal SVG icon
theme assets. A canonical button map generates the hwdb, C header, documentation
table and visual reference, with a build-time drift check.

The X server is started by xinit within the existing PAM/runit session. Matchbox
owns maximization; Qt apps no longer request framebuffer-specific window hints.
The package and fresh-image dependency paths use the same stack. No parallel
legacy display platform is maintained.

## Deliberately retained glue

The session adapter still bridges physical keyboard activity into elogind's tty
idle accounting and supplies dim/Hold/audio state to optional helpers. This is
not replaced with a new custom power manager or an unmeasured desktop service.
A future standard X session power-manager integration could replace that bridge;
it needs separate idle, AC/battery and inhibit validation. Current sleep, volume,
battery and mixer ownership remains with the already tested Linux services.

The small JSON app catalog and APT job status are presentation metadata, not an
alternative package format. AppStream is a possible later catalog improvement;
there is no reason to introduce another package manager. Camera and USB protocol
work are actual hardware gaps, not general-purpose Linux features to rebuild.
Apps remain root, as requested; permission separation is outside this audit.

## Hardware checks and remaining limitation

The Reader runs ReRead 0.17.0 under Xorg, not a framebuffer app backend. Runit
restart replaced the X server cleanly with one server left running. All five
in-repo apps have been rebuilt for UI SONAME 4; old binary compatibility is not
maintained. Focused Qt input checks cover press, repeat, release and Zoom, and
ignore synthetic Hold releases. Files and Software navigation checks passed.

An initial Camera suspend attempt froze the Reader and USB failed to enumerate;
its power log contains a prepare entry but no resume entry. A forced power cycle
was required. Subsequent Home-only and Camera sleep/wake tests both passed:
the user confirmed navigation and live preview, and SSH confirmed unlocked input,
a resumed camera stream and successful resume hooks. The failure has not been
reproduced or explained; these passing checks do not establish a fix for it.

On the rebuilt UI version 4 packages, Files, Software, Internet Archive, Booru
and Camera each became the active X window and returned focus to Home when
closed with Favorites. Their loaded library mappings confirmed SONAME 4. The
user confirmed physical navigation, Hold lock and Hold unlock all worked.
Xorg owns console graphics/keyboard modes; the session adapter no longer changes
or restores Linux keyboard mode itself.

The final core package also passed a normal reboot. In the subsequent Camera
sleep/wake check the user confirmed preview and direction buttons returned
without touching Hold; SSH confirmed `locked:false`, one X server, a live frame
counter, clean `dpkg --audit`, and a completed resume hook. Kernel captures for
these checks are retained under ignored `build/xorg-migration/`.

## Image opening, 0.18 development

Files now calls GIO's default URI handler. Images is a separate MIME-associated
Qt application with a desktop entry and icon; system defaults are standard
`/etc/xdg/reread-mimeapps.list`. XDG_DATA_HOME is user-wide rather than inherited
as a caller's private directory. Existing explicit app data paths are preserved.
The user confirmed physical zoom, pan, rotation and return to Files on hardware.
Offscreen tests cover GIO filename preservation, view-only rotation, error exit,
and existing Files navigation/deletion. Text/audio and other apps' viewing
actions remain for later steps. No new installer image has been built for this
step; source fresh-image dependencies/default packages include Images.

Booru 0.3 moves full image samples to GIO/default Images. Its old full-image
renderer and zoom/pan implementation are removed. A bounded 64 MiB XDG cache
keeps image files available across app exit; explicit saves remain in shared
storage. Tests cover cache eviction, cached GIO opening, unchanged files on
non-image responses, and cache lifetime. An isolated on-device post fixture
opened Images and returned to Booru without network or account requests.

## Shared media opening (0.19)

Files, Booru, Camera review and Internet Archive now use freedesktop MIME defaults
and GIO for full-file opening. Standalone Images (Qt codecs), Text (Qt document
layout) and Audio (GStreamer playbin/ALSA) replace embedded viewers. Archive
no longer carries its custom MP3 network/decoder/PCM worker or text pagination.
The audio UI owns no codec, mixer or power daemon; it uses login1 idle inhibitors
and pauses on its PrepareForSleep signal. Runtime dependencies are distro
GStreamer base/good/ALSA plugins, not FFmpeg or a desktop environment.
