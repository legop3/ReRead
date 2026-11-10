# Xorg trial on the Intel Reader — 2026-11-10

Result: Xorg is a practical migration candidate. Existing Qt raster applications
work with the `xcb` platform backend, without a desktop environment or an OpenGL
renderer. The window manager can take over visibility/focus rather than apps
cooperatively sharing `/dev/fb0`. This was a temporary on-device trial, not a
production migration or installer change.

## Stack and measured cost

- antiX kernel 5.10.256-antix.1-486-smp; Xorg 21.1.16, fbdev driver,
  libinput, Matchbox window manager; existing 960×544 panel mode.
- Qt uses raster drawing, `QT_QPA_PLATFORM=xcb`, `QT_XCB_GL_INTEGRATION=none`.
  Xorg GLX/DRI modules are disabled; fbdev's default shadow framebuffer is enabled.
- Packages, including diagnostic tools: 36 new packages, 16.6 MB installed.
- Xorg + Matchbox idle PSS: approximately 15 MiB. Combined Home/session/server/WM
  idle PSS was about 40 MiB versus 29 MiB for Home/session under LinuxFB.

| Workload | LinuxFB | Managed X11 windows |
| --- | --- | --- |
| Total-machine Home CPU | about 4% | about 5% |
| Total-machine camera-preview CPU | 95.1% mean | 95.3% mean |
| Camera backend frames generated per second | 11.6–12.1 | 11.8–13.9 |

Three consecutive 10-second samples per condition. Total CPU uses both logical
CPUs as 100%; Xorg itself used about 6.9% of one logical CPU during camera
preview, with less CPU spent in the Qt camera process. The complete pipeline's
CPU was effectively unchanged in this short trial. Frame rates here are deltas
of the native backend's frame counter across the same timed intervals, not
single spot readings of its `preview_fps` field and not measured panel refresh
rates. Home measurements include some interactive verification. No battery
runtime claim or statistically significant speed improvement is established.

## Input and window ownership

The isolated trial helper disables app-level evdev readers for X11 and receives
Qt key press/release events from the focused X window. Native X keycodes are
translated to the existing physical-button IDs. Hold, Power, volume, activity,
dimming and inhibitors still use the existing system services/session adapter.

Linux Zoom keycodes 418/419 exceed X11's core keycode range. The trial temporarily
maps their physical AT scans 0x69/0x6a to Linux F13/F14 (183/184), preserving the
ReRead semantic Zoom IDs in the trial helper. The controller restores the
original kernel mappings when it exits. A production implementation should
record an X-compatible hardware transport map and appropriate XKB symbols.

Home remains mapped beneath applications rather than hiding for each launch.
The shared trial helper uses ordinary managed window hints under X11 instead
of the framebuffer-oriented frameless/fullscreen hints. Matchbox maximizes
windows and suppresses titlebars. An ordinary `xmessage` window received both
X input focus and `_NET_ACTIVE_WINDOW`, then Home became active when it closed.
Camera also became the active managed window. Thus the compositor/window
manager, rather than Home's child watcher, can own display and input handoff.
Home's existing process bookkeeping remains in this prototype; this trial does
not establish that every app lifecycle or nested file-opening path is migrated.

The first trial restart left the old X session alive because the shutdown path
failed to terminate xinit's detached process groups. The controller was fixed
to terminate the trial X server explicitly, and the window tests were repeated
in a fresh session with the updated helper. Earlier ordinary-window activation
attempts failed with the framebuffer-oriented window hints and are not counted
as successful tests.

## Checks and remaining migration work

The user confirmed Home looked normal and physical navigation worked. Camera
preview ran through X11. Existing Linux-backlight dimming was checked with a
temporary five-second timeout; key activity restored normal brightness. Existing
triggerhappy/ALSA volume handling worked in both directions. Its asynchronous
mixer update exceeded the first check's short wait under camera load; polling
confirmed the update and the original 25% volume was restored. Display settings
were restored after checking.

Sleep/wake was not exercised because availability to press Power had not been
confirmed. It remains required before a production switch. Every application's
input path, Hold behavior and crash handling also need migration validation;
in particular, apps using Device directly rather than Screen cannot assume
that raw evdev callbacks will continue under X11.

Xorg does not replace elogind suspend/inhibitors, UPower, ALSA, or the kernel
backlight interface. It supplies window management infrastructure, focused input
and the X idle extension. Those existing Linux services can remain in place.
Standard GIO/MIME launching becomes easier once multiple independently launched
windows can coexist; no MIME handlers were implemented in this trial.

Private helper variants, scripts, raw samples and device logs are in ignored
`build/xorg-trial/`. Production binaries, boot configuration and installer remain
unchanged. The small distro package stack remains installed for further trials.

References: [fbdev driver](https://manpages.debian.org/trixie/xserver-xorg-video-fbdev/fbdev.4.en.html),
[Matchbox](https://manpages.debian.org/trixie/matchbox-window-manager/matchbox-window-manager.1.en.html).

Files was additionally launched directly from SSH as an independent X11 client,
without Home spawning or watching it. It became active, closed normally on Back,
and Home automatically regained focus. At the end the trial controller stopped
Xorg, restored Linux Zoom codes 418/419, and restarted the original LinuxFB
session. No permanent display-backend migration was made.
