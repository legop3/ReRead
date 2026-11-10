# Files

An independent Qt/X11 program, packaged as `reread-app-files`. Its standard
`.desktop` entry and `reread-files` SVG icon put it on Home. Removing the app
removes the browser, never shared files. No background file service is added.

Files uses `REREAD_FILES_DIR` (default `/var/lib/reader/files`) and normal Qt
filesystem APIs. The web browser and Camera use that same directory. It provides:

- A dedicated browser layout: folder path, compact icon rows, name/type/size
  columns and a selected-item inspector with modification time and position.
- Folder navigation with remembered parent selection and live directory updates.
- Name, newest-first or size sorting, with folders first.
- Normal GIO/MIME opening: images, text and audio launch their separate default
  viewers. Other installed handlers are used for their advertised file types.
- Missing associations show file details and suggest installing a viewer.
- File/folder details, size and modification time.
- Permanent file or recursive folder deletion, with Cancel selected by default.

Arrows select items; OK opens; Back goes up and returns Home from the shared root.
Options opens Details, Delete, Sort and Refresh. Favorites returns Home directly.
Closing a viewer returns to the same folder and selection in Files. There are no
embedded text or image viewers. The app does not browse outside
the shared root or follow directory symlinks. Deleting a folder removes its
contents; there is no trash/undo. Unsupported formats and naming operations such
as rename/new folder remain available through the web file manager.

The standard release builder builds this app and its package. To rebuild only
its executable after the shared libraries have been built:

```sh
antix/apps/files/build.sh build/antix/ui
```

`test-navigation.cpp` exercises navigation, real GIO dispatch to an isolated MIME
handler (including a filename with spaces, quotes, Unicode and URI metacharacters),
text dispatch, details, live updates,
canceling deletion, and confirmed deletion of disposable files/folders offscreen.
It also runs the real entry point to verify GUI input does not open raw evdev. It never targets the Reader's shared data.

Hardware validation for 0.1.1: one evdev OK press enters exactly one directory;
Back at the shared root and Favorites both exit with status 0. These checks used
a disposable directory, leaving shared user files untouched.
