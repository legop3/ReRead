# Gopher for ReRead

A native button-only Gopher client for the Intel Reader.

- Open **Browse Gopherspace** to visit Floodgap's directory. Follow ordinary
  Gopher menus, including links to other servers, without an on-screen keyboard.
- Plain text pages are rendered inside Gopher with scrolling and Zoom font size.
- Use **Options** for reload, bookmarking, viewing the address, forward,
  bookmarks, history, or Home. Back returns through visited pages.
- Gopher search items are present but unavailable until ReRead has shared
  system text entry. No private keyboard or web settings are provided.
- Binary items prompt before download; saved files go to
  `$REREAD_FILES_DIR/Downloads/Gopher/` (normally
  `/var/lib/reader/files/Downloads/Gopher/`). Successful transfers use
  atomic writes and launch the default MIME handler through GIO.
- The Guide button's hold overlay and Favorites exit are owned by ReRead.
- Downloads are limited to 128 MiB and menu/text responses to 2 MiB.
  Bookmarks are saved under the application-specific XDG data directory.

## Building

From the ReRead repository checkout, with the normal Docker build environment:

```sh
docker build -t reread-app-builder image-builder
docker run --rm -v "$PWD:/work" reread-app-builder sh -c '
  antix/ui/build.sh build/antix/ui &&
  python3 antix/apps/gopher/package.py
'
```

Output: `out/antix/reread-app-gopher_0.1.0_i386.deb`.

Alternatively, use the shared release tool: `./release.py app gopher`.

Install through the Reader's web Apps page or with
`reader-update install /path/to/reread-app-gopher_0.1.0_i386.deb`.

## Implementation

Uses ReRead::Screen (UI 5), Qt5Network for direct TCP Gopher requests,
QSaveFile for downloads, and GIO for opening files. No browser engine,
extra services, or modifications to the ReRead core are needed.

Before publishing, test a real Reader against live Gopher servers; verify
focus, sleep behavior, and opening files via the installed MIME handlers.
