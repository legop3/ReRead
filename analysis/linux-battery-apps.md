# Standard Linux battery and application launching — ReRead 0.10.0

ReRead now delegates battery policy to UPower and desktop-entry processing to
GLib/GIO. Apps continue to run as root, as requested. No desktop environment or
new application framework is introduced.

## Battery ownership

The distribution's UPower daemon reads Linux power_supply devices, aggregates
battery state, calculates warning levels and performs critical-power actions
through login1. Its packaged D-Bus service activates it normally. There is no
ReRead replacement for those responsibilities.

`reader-battery` remains only as a signal-driven presentation adapter: it
subscribes to UPower property changes and publishes the existing optional
`/run/reader-battery.json` and text status for the framebuffer UI. The web API
reads UPower directly. No five-second battery polling, threshold decision loop,
shutdown subprocess or independent countdown remains in that adapter.

The one-time package migration transfers the previous default reserve threshold
to `/etc/UPower/UPower.conf`: low 12%, critical 7%, action 6%, PowerOff. Existing
custom UPower thresholds are respected. Subsequent updates preserve this normal
UPower configuration. `/etc/reader/power.json` is obsolete; it is read only during
the first migration. UPower supplies its own 20-second action delay. The UI shows
an imminent-action notice rather than trying to maintain a separate countdown.

The Reader's firmware can report Discharging while AC is online and charging is
paused. The adapter labels this Not charging for presentation, retaining the
UPower state as `raw_status`. It does not override UPower's warning/action
levels. In particular, UPower may warn when all batteries report discharging
even with AC online; this is its standard policy rather than ReRead's former
AC-gated shutdown logic.

Legacy nullable charge/current fields remain for API compatibility. Voltage is
provided by UPower's physical battery device; aggregate energy, full energy,
power and time-to-empty fields use its native units. Applications can bypass
the optional JSON snapshot and use `org.freedesktop.UPower` directly.

Sources: [UPower D-Bus API](https://upower.freedesktop.org/docs/UPower.html),
[device properties](https://upower.freedesktop.org/docs/Device.html),
[installed-version policy implementation](https://sources.debian.org/src/upower/1.90.9-1/src/up-daemon.c/).

## Desktop entry ownership

Home uses `GDesktopAppInfo` for discovery, localised names/descriptions, XDG
visibility rules, TryExec, quoting/escaping and Exec field-code expansion.
GIO honours `Path=` and starts the executable without adding a shell. The
installer's pre-install icon validation uses GLib KeyFile, not Python INI rules.
Qt's existing icon-theme lookup still draws application artwork.

The desktop identity is `ReRead`. Foreground entries must use Terminal=false
and omit/disable DBusActivatable: they need a directly tracked foreground
process. ReRead continues to own handing the framebuffer/navigation to that
process and returning Home when it finishes. GIO's child setup callback applies
the existing Linux parent-death rule; a pidfd observed by Qt reports child exit
without polling. Applications do not need to link a ReRead library.

Source: [GIO managed launching with file descriptors](https://docs.gtk.org/gio-unix/method.DesktopAppInfo.launch_uris_as_manager_with_fds.html).

## Validation and limits

- The real Reader's UPower reports battery, charger state and active charging.
  Charger unplug/replug changed OnBattery correctly in both directions.
- Actual UPower property signals updated the UI snapshot. Stopping UPower
  exercised D-Bus reactivation and runit adapter recovery; PowerOff remained
  the selected critical action.
- A disposable app installed/removed through the normal app manager exercised
  GIO quoting, `%c`, `%k`, `%%`, `%i`, absent document arguments, `Path=`, root
  UID and ReRead data-directory environment. Normal exit and crash returned
  Home; killing Home terminated the independent child. The fixture was removed.
- GLib parsed UTF-8 package metadata and rejected a missing foreground icon.
- Battery adapter fixtures cover units, the AC presentation quirk, unmasked
  action warnings, absent batteries and an unavailable daemon. They do not
  implement or simulate a replacement critical-shutdown policy.
- A full reboot started Home/GIO and the D-Bus-activated UPower daemon normally.
  The restarted web service returned live UPower readings through its battery API.
- Native binaries build for i386 with warnings treated as errors.

Physical deep-discharge shutdown has not been exercised. Userspace battery
protection does not run while the machine is in S3. Existing USB installer
images are not refreshed by building update packages; the normal full build
includes these source changes and distribution dependencies.

Ignored local logs and disposable fixtures: `build/antix/linux-standard/`,
`build/antix/upower-*.log`, `build/antix/linux-standard-release.log`.
