# Independent applications and optional device helpers

ReRead 0.7.0 separates device integration from drawing. Applications remain
ordinary executables registered with a desktop entry and packaged with dpkg.
They can choose their own toolkit and rendering, with no mandatory ReRead base
class or library.

`libreader-device.so.1` provides optional input, Hold, volume, idle dimming and
power integration. Its only Qt dependency is QtCore. It starts inactive and
must be activated by the foreground owner. `libreader-ui.so.3` adds the optional
Qt Widgets Screen/drawing conveniences. Home and Camera choose to use them.
Camera's local HTTP client now belongs to Camera, so neither helper requires
QtNetwork. The existing Linux audio, backlight and power mechanisms are unchanged.

Home sets the Linux parent-death signal before executing every foreground app.
This lifecycle behavior applies even to apps that link no ReRead library.
Applications must stay in the foreground and clean up their own worker processes.

## Validation on 2026-11-09

- Built core 0.7.0 and Camera 0.3.0 packages and installed them together on the
  Reader. Installed Device, UI, Home and Camera binaries matched the local builds.
- Inspected ELF dependencies: Device requires QtCore only; the independent
  QWidget example links Device without the ReRead drawing library.
- Ran a QtCore-only check in an isolated builder container: inactive input,
  Hold suppression/unlock and asynchronous power/resume callbacks passed.
  Its power command was a stub; this was not another physical suspend test.
- Launched the independent QWidget example through Home on the Reader. Its own
  layout rendered correctly; Select, Hold, volume, idle dim/wake and Back passed.
  Temporary idle and audio settings were restored and its desktop entry removed.
- Launched plain `/bin/sleep 60` through Home, then restarted Home. The child
  terminated without linking any ReRead library; its temporary entry was removed.
- Camera live preview and saved-photo review rendered correctly. Opening its
  options stopped streaming after the lease expired; closing options resumed it.
  Returning Home stopped streaming again.

The standalone example stays in source as a developer reference and is not
installed by default. Existing installer images were not regenerated for this
library split; the maintained build scripts package these versions on the next
installer build.
