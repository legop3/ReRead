# ReRead app packages

Apps are ordinary Debian packages named `reread-app-<id>`. The ReRead **Apps**
page installs or updates their `.deb` files, lists installed versions and
descriptions, and removes them without purging configuration or app data.
Dependencies are handled by apt. There is no app store or second package format.

Use `Architecture: i386` for native code, or `all` for architecture-independent
apps, and `Depends: reread (>= 0.1.0)` plus the app's real dependencies.
Keep the same package name across releases and increment its Debian version.

An optional `/usr/share/reread/apps/reread-app-<id>.json` provides display metadata:

```json
{
  "name": "Example app",
  "description": "A short description",
  "service": "reread-app-example"
}
```

Version information comes from dpkg, not the JSON file. An app without metadata
still appears in the manager using its package description. The `service` field
is optional. When present it must equal the package name, with an executable
`/etc/sv/<package>/run`. The manager then provides persistent Enable/Disable
controls. Services run under runit and start again at boot when enabled.
Do not ship an enabled runsvdir symlink; the manager owns that choice.

Package application code under `/usr/lib/reread/apps/<id>/`, static assets under
`/usr/share/reread/apps/<id>/`, and configuration under `/etc/reread/apps/`.
List editable configuration files in `DEBIAN/conffiles`. Runtime data belongs
under `/var/lib/reader/apps/<id>/`; the app should create its data directory as
needed. **Never include runtime data in the package or delete it in removal
scripts.** Apps must not replace ReRead's core services or reset global settings.

Build with the usual `dpkg-deb --root-owner-group --build package-directory`.
Install/update through Apps, or `reader-update install /path/to/app.deb` over SSH.
Remove through Apps, or `reader-update remove reread-app-example`.

The current platform provides package management and optional background-service
controls. There are no bundled apps yet. A button-operated foreground app launcher
will be added when the first device applications establish their UI requirements.
