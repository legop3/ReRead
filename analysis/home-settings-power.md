# Home, settings and optional automatic sleep

ReRead 0.8.0 adds a three-column Home grid, grouped settings and opt-in automatic
suspend. Camera 0.3.1 supplies its icon in the standard hicolor theme. Apps remain
independent Linux executables; none of these changes requires a particular UI
toolkit for applications.

## Behavior

- Home displays six tiles per page. D-pad navigation follows the grid, clamps
  at its edges and handles a partial final row. The selected tile has an outline,
  bold label and description. There is no animation or mandatory control bar.
- Settings has Display, Sound, Power, Connections and About categories. Back
  returns to the prior category selection. Brightness, volume and timeouts use
  Left/Right; actions use OK.
- Power / Auto sleep after offers Off, 2, 5, 10 and 30 minutes without button
  activity. Off is the default, including preserved configurations without the
  new `auto_suspend_seconds` field. It applies on battery and external power.
- The optional Device helper handles the timer in Home and Camera, calling the
  same Linux suspend helper as the Power button. Hidden apps stop their timers;
  input, settings changes and successful resume restart them. Standard package
  flock ownership defers automatic sleep without waking an idle app or
  cancelling its dim timer. Manual Power behavior is retained.
- Foreground apps must declare `Icon=` in their desktop entry. The app updater
  checks this metadata before installation using a streaming dpkg archive read;
  background-only apps need no icon. Qt resolves theme names or absolute image
  paths. Broken artwork gets a fallback rather than hiding an installed app.
- The small ReRead system icon theme inherits hicolor. LinuxFB supplies no icon
  search directories on this installation, so Home explicitly configures the
  standard user/system icon and pixmap locations. SVG rendering uses the distro
  Qt SVG plugin. Original system SVG artwork lives in `antix/ui/icons/`.

## Validation, 2026-11-09

Built the maintained core and Camera packages and updated the actual Reader.
Real input checks covered Home/Settings navigation, enabling/disabling auto sleep,
brightness and dim values, and Camera live preview/return. Screenshots confirmed
the final icon grid and category layout on its framebuffer. Temporary apps checked
named, absolute and missing-icon fallback artwork, a second grid page and its
partial final row. Those entries were removed and original settings restored.

An isolated QtCore check with a stub sleep command covered opt-in/off behavior,
activity reset, inactive ownership, package-lock deferral and resume callbacks.
No new unattended physical suspend was attempted; the real Linux deep-sleep/wake
path was verified previously in `reread-ui-validation.md`.

Package checks accepted Camera's named icon and an absolute icon entry and
rejected a missing icon. Python/SVG syntax and diff whitespace checks passed.
The optional VM UI check now follows grid navigation; its border predicate was
checked against the Reader framebuffer. A new full image/VM install cycle was
not run for this change. Existing installer images remain older; the next normal
installer build includes these source and package changes.

Ignored evidence: `build/antix/home-grid-*.log`, `auto-sleep-check.log`,
`icon-package-check.log`, `home-final.png`, `settings-final.png` and
`home-grid-page-*.png`.
