# Shared files and web management — ReRead 0.14.0

The owner chose to retain USB networking and use a better web file manager,
rather than add MTP or a dedicated mass-storage partition. No gadget driver,
USB descriptor, addressing, DHCP configuration, partition layout or filesystem
allocation was changed.

The public root remains `/var/lib/reader/files`, with `REREAD_FILES_DIR` exported
to apps. Camera now writes `Pictures/Camera/` and returns relative paths. The
on-device Files browser navigates directories. Private state remains outside the
shared root. See `antix/STORAGE.md` for the application contract.

Copyparty 1.20.26 is pinned, checksum-verified and bundled with upstream license
notices. It binds to the existing USB address on port 8080 and runs under the
existing USB management-session owner. The Files tab embeds it and offers a
full-screen link. A supported branding hook adds readable toolbar labels. No
background indexing or server-side thumbnail/transcoding dependencies are enabled.
The standard distro Python Jinja2 package supplies HTML rendering.

Validated on the physical Reader after upgrading 0.13.0 to 0.14.0:

- Folder creation, upload/download round trip, rename, move, ZIP download and
  deletion through the real upstream HTTP/WebDAV implementation.
- Chromium rendering of the embedded upstream UI, navigation into folders,
  creation of a folder through the UI, confirmed upload through the actual up2k
  widget, and management-tab switching without recreating the frame.
- No JavaScript page errors during that browser flow; test folders cleaned up.
- A real camera capture appeared at `Pictures/Camera/photo-20261109-154428-b6e628.jpg`
  with that same relative filename in camera metadata. Existing user content was
  retained. The shared directory has no file-manager database/history files.
- An offscreen native Qt launcher check passed: open root, navigate two folders,
  open a nested photograph, then Back through both parents to Home.
- Existing management-page browser checks passed for Wi-Fi profile editing,
  repositories, app/update controls and cached operation results.

The fresh SSD/USB images and platform package were rebuilt. Offline `e2fsck -fn`
passed on the final ext3 image. The partition table still has only the 127 MiB
boot partition and 3,690.88 MiB root partition, filling the known 4,004,388,864-byte
SSD apart from the 1 MiB alignment gap. No fresh physical reinstall or new
sleep/power measurement was performed for this release. USB file service used
about 40 MiB RSS after the interactive checks; that is not an idle-power result.

Cold file-manager startup takes several seconds on the Atom. The Files tab waits
for its HTTP listener before loading the frame. After an in-place update, USB
reconnection or reboot loads the added management child; the development Reader's
management session was restarted explicitly without unloading the gadget driver.
