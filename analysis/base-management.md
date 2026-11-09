# ReRead 0.13.0 base-management validation

Validated 2026-11-09. Default applications are Camera 0.4.0 and Software 0.1.0.
The OS remains antiX Core i386 with the existing hardware-tested kernel.

Software is a separate Qt executable and Debian package with a normal desktop
entry and hicolor SVG. Its local `reader-software` CLI uses the same detached
APT/dpkg worker as web management. No additional daemon or app framework is added.

APT source management writes real `.sources`/`.list` files and `Signed-By` keys.
libapt's SourceList parser validates proposed edits before replacing files; it
avoids loading the whole distribution package cache for a configuration edit.
App catalogs/checks include `reread*` source files, accept trusted origins without
hardcoding a host/suite, and use Debian candidate/version rules.

Wi-Fi profile IDs and persistence belong to wpa_supplicant. `list_networks`,
`get_network`, `set_network` and `save_config` handle profiles; there is no parallel
network database. Passwords are not returned to browsers. Blank-password edits
preserve credentials when SSID/security are unchanged. Explicit selection restores
enabled alternatives without interrupting the selected network. Configuration stays
mode 0600, with derived PSKs for profiles created through the UI.

Checks completed:

- Actual Reader: Software opened from Home through Linux input, ran a repository
  check via its local CLI, and returned Home. Framebuffer screenshot verified the app.
- Actual Reader: additional personal profile saved, edited with a blank password
  and negative priority, then removed; the existing profile/connection remained.
- Actual web interface/backend: repository add, disable, raw edit and removal;
  existing source files compared unchanged afterward. Temporary source was not fetched.
- Actual web interface/backend: Wi-Fi add, edit and targeted forget; the existing
  saved network and association remained. No second live AP authentication was tested.
- Real APT parser rejected malformed source edits without replacing the old file;
  package lock prevented changes during jobs. Source scoping excluded distro files.
- Disposable real wpa_supplicant (`none` driver, container loopback): profile
  persistence/reconfigure, blank-password edits, priorities, selection/fallback flags,
  mode 0600 and targeted deletion.
- Qt button-navigation test exercised check, app install, Cancel-first removal
  confirmation and platform-update dispatch through real QProcess calls to a fixture.
- Browser checks exercised update/app controls, busy/error states, cached results,
  no automatic internet checks, saved-network forms and repository controls.
- 512 MiB VM: automatic USB discovery, streamed installation onto a disposable
  Reader-size SSD, full-disk allocation and automatic reboot. Installed OS booted,
  opened/returned from both Software and Camera, and passed two suspend/wake cycles.

The final libapt-parser/browser-pattern corrections were repacked into the platform
package and installer using the same filesystem/image assembly routines. The source
build entry point includes all of them; this is one maintained antiX-based image.
