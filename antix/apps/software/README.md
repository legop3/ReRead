# Software

A default, independent Qt app for on-device platform/app package management.
Its overview combines navigation with installed-version, update and job status.
The app catalogue uses desktop-entry icons, compact status rows and a description
pane. Operations show their actual state and a scrollable monospaced log.
Unavailable app icons use the normal generic fallback; no percentage is invented.
It uses the optional ReRead screen/device drawing helpers, standard desktop-entry
and icon files, and the core's `reader-software` command. No USB web service or
additional daemon is involved. It works over any configured internet connection.

`reader-software status` returns the existing package inventory, cached catalog and
persistent job/log as JSON. `check`, `install <package>` and `remove <package>`
start detached jobs through the same manager as the web interface. Packages and
dependencies remain owned by APT/dpkg; this app supplies button navigation.

Build with `build.sh <ui-build-directory>` after building the platform UI libraries.
`antix/release.py` does this and builds `reread-app-software`; fresh installers include
it alongside Camera. The app version is independent of the platform version.

For a focused navigation check inside the disposable builder, run
`python3 /work/antix/apps/software/test-navigation.py`. It builds `test-navigation.cpp`
and provides a temporary CLI fixture. The test checks check/install/remove/system
update dispatch through real Qt process calls; package transactions are verified by
the backend and physical Reader checks.

UI 5 supplies the shared physical-button hints: hold Guide to show them over
the current screen, release to hide. No footer space is reserved. The shared
SVGs match the Reader buttons; apps do not provide private glyphs or Guide handlers.
