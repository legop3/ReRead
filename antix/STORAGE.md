# Shared files

ReRead has one shared-files root: **`/var/lib/reader/files`**. It is an ordinary
folder on the existing Linux root filesystem. Files, applications and system
updates draw from the same free SSD space. There is no separate data partition,
reserved transfer volume, disk image, import step or synchronization copy.

The launcher exports `REREAD_FILES_DIR=/var/lib/reader/files` to applications.
Programs may read that environment variable or use the documented absolute path;
no SDK or file service is required. Use normal Linux file and directory operations.
The web file manager and the independent `reread-app-files` application access
this exact directory. Files can be updated or removed without changing storage.
Its Delete action permanently removes the selected file or folder after a
confirmation with Cancel selected by default.

## Folders and application data

Shared files are user content, not application internals. Use folders:

- `Pictures/Camera/`: new camera photographs and document captures.
- `Documents/`, `Music/`, `Downloads/`: suggested locations for corresponding content.
- Other folders are welcome; these names are conventions, not a closed schema.

Create a folder when an application first needs it. Keep filenames meaningful and
avoid collisions; do not assume the root is a flat list of photos. Camera's API
returns filenames relative to the shared root, including any subdirectories.
Existing files stay where the owner put them. Apps must handle files being moved,
renamed, replaced or deleted through another app or the web file manager.

Private app state still belongs in `READER_APP_DATA_DIR` (normally
`/var/lib/reader/apps/<desktop-basename>`), with `XDG_DATA_HOME` pointing there.
System configuration belongs in `/etc`; package/cache/transfer bookkeeping belongs
outside shared files. Removing an app must preserve its user files and data.
Reinstalling the OS still replaces SSD contents; in-place updates retain them.

## Web file manager

**Files** in the existing USB web interface embeds Copyparty, an upstream Python
file server. It provides folder navigation/creation, drag-and-drop and resumable
uploads, downloads, selection, rename/move/delete, and browser media previews.
**Open full screen** opens its complete interface directly at
`http://192.168.7.1:8080/`. Assets are bundled; no internet connection or CDN is
needed. The management page remains on port 80.

The file manager is a child of the existing USB management session and binds only
to `192.168.7.1`. It stops with the other management programs when USB disconnects.
It does not change gadget mode, Ethernet configuration, DHCP or SSH. The connected
USB host can manage shared files without a second account or permission setup.

Configuration: `/etc/reader/files.conf` (a normal Copyparty config file).
History/config metadata: `/var/lib/reader/file-manager/`, outside shared files.
Logs: the existing `/var/log/reader/usb.log`. The default configuration leaves
64 MiB free for the system when accepting uploads, disables server-generated
thumbnails/transcoding, and does not enable background library indexing. Image,
audio and video previews use the computer's browser and its supported formats.

## Recovery and compatibility

No filesystem migration or repartitioning is needed for this release. The old
flat `/files/<name>` HTTP upload/download endpoints are replaced by Copyparty's
interface; use its HTTP/WebDAV endpoints for scripting. The management
`/api/status` response exposes `files_root` and free/total filesystem space.
After updating from a version without Copyparty, reconnect USB or reboot to start
its added management-session child. Future app packages must not install content
into the shared root or wipe it in maintainer scripts.
