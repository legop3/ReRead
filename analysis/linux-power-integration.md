# Standard Linux power and input integration

Investigated on 2026-11-09 against the running ReRead 0.8.0 Reader, its configured
apt repositories, downloaded distribution packages and upstream documentation.
This is an integration recommendation, not an implemented migration. No packages
were installed and no running input or power services were changed. Downloads
and extracted files on the Reader are confined to `/tmp/reread-power-research`.

## Recommended direction

Use **elogind** for sleep policy and standard inhibitors, **triggerhappy** for
global volume shortcuts, and **udev hwdb** for the calibrated keyboard mappings.
Retain the Linux backlight driver, ALSA and kernel suspend support already in use.
Keep only the necessary framebuffer-session and Reader hardware integration in
ReRead. No desktop environment or systemd PID 1 is required for this direction.

Elogind implements the standard `org.freedesktop.login1` D-Bus interface. It
supports sleep requests, idle policy and power-button handling independently of
systemd. Its inhibitor interface gives applications a shared way to prevent
automatic sleep or delay preparation; applications can use D-Bus or the
`elogind-inhibit` command instead of a ReRead-specific API.
[Upstream](https://github.com/elogind/elogind/blob/main/README.md),
[inhibitor tool](https://manpages.debian.org/trixie/elogind/elogind-inhibit.1.en.html).

## What the actual antiX installation provides

Despite antiX describing its default system as elogind-free, the Reader's
configured repositories contain an antiX-built i386 elogind package. The package
is real, rather than an empty compatibility placeholder.

| Component | Candidate on this Reader | Finding |
| --- | --- | --- |
| elogind | 255.17-1.0antix3 | Strongest fit for login1 sleep/inhibitor APIs |
| libpam-elogind | 255.17-1.0antix3 | Session registration; needs actual PAM integration |
| triggerhappy | 0.5.0-1.1+b3 | Small system-wide hotkey service, suitable without X/Wayland |
| acpid | 1:2.0.34-1.0antix2 | Useful event-to-script tool, but does not replace session/idle/inhibitor policy |
| consolekit | 1.2.6-4+b1 | Alternative session manager; different API, with pm-utils for Linux sleep |
| pm-utils | 1.4.1-21 | Sleep/hook machinery, rather than the complete missing policy layer |
| seatd | 0.9.3-1.0antix1, installed | Existing seat/device access service; does not supply login1 power policy |

The combined simulated installation was:

```sh
apt-get -s --no-install-recommends install elogind libpam-elogind triggerhappy
```

Result: **five new packages, zero upgrades and zero removals**: elogind,
libelogind0, libelogind-compat, libpam-elogind and triggerhappy. No systemd init or
desktop was requested. The four elogind packages total about 1.37 MB downloaded
and 4.3 MiB extracted; triggerhappy declares 113 KiB Installed-Size. These are
package sizes, not measured daemon RAM or CPU costs.

The downloaded elogind binary successfully printed its version on the actual
Atom Reader using its extracted libraries. This checks basic executable/library
compatibility only; the daemon was not started.

## Concrete integration work still needed

### Input ownership

`Device::discover()` currently grabs every EV_KEY input device with EVIOCGRAB.
That includes the AT keyboard carrying the Reader buttons and the separate ACPI
Power Button device. The kernel grab is exclusive: installing another event
consumer does not make it receive those events.
[Kernel input documentation](https://docs.kernel.org/driver-api/input.html).

Before enabling system-wide consumers, remove that blanket ownership model.
Leave system power/volume handling outside app lifetimes; keep foreground
navigation delivery and console suppression working. Elogind can filter its own
input stream to power/sleep keys using normal evdev masks. Determine whether
physical Power is reported by the AT keyboard, ACPI device or both before
choosing devices/rules, to avoid duplicate suspend requests. Preserve the user's
short-press behavior and firmware long-press shutoff.

Triggerhappy can bind volume press/repeat events to the existing ALSA helper,
independently of the foreground app. Its configuration and hotplug support are
provided by the existing project. It is not itself a replacement compositor or
an input-routing solution for Hold suppression.
[Upstream](https://github.com/wertarbyte/triggerhappy),
[manual](https://manpages.debian.org/trixie/triggerhappy/thd.1.en.html).

### Put scan mappings in udev

The running udev is version 251 and already includes the hwdb/keyboard rules.
The Reader exposes a useful DMI match: IntelCorporation / Colbert. A model-specific
hwdb entry can own all calibrated scan-to-Linux-keycode mappings, instead of every
app applying EVIOCSKEYCODE itself. For example, the applicable form is:

```text
evdev:atkbd:dmi:*:svnIntelCorporation:pnColbert:*
 KEYBOARD_KEY_66=camera
 KEYBOARD_KEY_de=power
 KEYBOARD_KEY_b0=volumeup
 KEYBOARD_KEY_ae=volumedown
```

This is illustrative, not a deployed complete keymap. Preserve all mappings in
`reader-keys.h` when migrating, including the calibrated Hold behavior.
[udev keyboard hwdb format](https://github.com/systemd/systemd/blob/v251/hwdb.d/60-keyboard.hwdb).

### Session and idle reporting

Home currently starts directly from runit without a PAM-managed session. Merely
installing libpam-elogind will not put that existing UI process into a session.
Its supplied PAM profile is interactive-session-only. The Reader's current
`runuser` PAM file does not include the common session stack, so it cannot be
assumed to solve registration unchanged either.

Register the handheld session through a normal PAM-backed startup path and
verify tracking of Home and launched apps. The framebuffer session must report
activity/idle through `SetIdleHint`. There is no X11/Wayland session manager here
to do that automatically. A small session integration layer remains justified;
it should report to login1 rather than invent another inhibitor protocol.
Check the effect of concurrent SSH sessions on system-wide idle policy.
[Idle policy](https://manpages.debian.org/trixie/elogind/logind.conf.5.en.html),
[session API](https://www.freedesktop.org/software/systemd/man/latest/org.freedesktop.login1.html).

### Startup and sleep lifecycle

The inspected antiX package supplies a SysV init script but no runit service
template. Its maintainer script contains compatibility branches for runit and
init-diversity; its D-Bus service file deliberately uses `Exec=/bin/true` instead
of starting the daemon. ReRead has init-diversity and runit, but no elogind service
directory. Verify startup through that distro integration or supply the normal
small runit service definition; do not rely on D-Bus autostart.

`loginctl suspend` returns after queuing a request, unlike the existing helper
which returns after wake. The current UI's return-callback behavior therefore
cannot simply be pointed at loginctl. Consume standard `PrepareForSleep`
notifications for app lifecycle. Apps/services preparing for sleep should use
delay inhibitors, with enough time for Camera's existing capture completion.

Reader-specific fan/audio/backlight restoration can use hardware sleep hooks.
Camera's pause failure must still prevent an unsafe transition. Elogind supports
configurable hook cancellation, but the default is not to cancel on a hook
failure. Verify its documented cancellation protocol, partial-failure recovery
and wake ordering. Ordinary apps should use inhibitor/signaling interfaces,
rather than install hardware hooks themselves.
[Sleep command and hook semantics](https://manpages.debian.org/trixie/elogind/loginctl.1.en.html).

## Next implementation step

First test the distro daemon and a properly registered framebuffer session with
automatic actions disabled. Exercise standard inhibitor acquisition/release
without sleeping. Then move input ownership and volume handling, migrate the
existing hardware preparation/restoration, and validate one real suspend/wake
with the owner present. Enable the existing auto-sleep setting only after its
idle reporting works. Retire superseded per-app policy as the service takes over;
do not maintain two active sleep-policy implementations.

Not yet validated: daemon startup on this runit/cgroup setup, physical power-key
delivery without app grabs, session idle reporting, real inhibitor effects,
sleep hooks and resource cost. The package simulation and version check do not
establish those results.

Local evidence, ignored by Git: `build/antix/power-research/` contains apt policy,
individual/combined install simulations, the inspected packages and maintainer
scripts, current udev/PAM/cgroup information and the Reader binary version check.
