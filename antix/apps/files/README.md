# Files

An independent Qt/LinuxFB program, packaged as `reread-app-files`. Its standard
`.desktop` entry and `reread-files` SVG icon put it on Home. Removing the app
removes the browser, never shared files. No background file service is added.

Files uses `REREAD_FILES_DIR` (default `/var/lib/reader/files`) and normal Qt
filesystem APIs. The web browser and Camera use that same directory. It provides:

- Folder navigation with remembered parent selection and live directory updates.
- Name, newest-first or size sorting, with folders first.
- Image preview, previous/next image, zoom and panning.
- UTF-8 text previews up to 256 KiB; other types show file details.
- File/folder details, size and modification time.
- Permanent file or recursive folder deletion, with Cancel selected by default.

Arrows select items; OK opens; Back goes up and returns Home from the shared root.
Options opens Details, Delete, Sort and Refresh. Favorites returns Home directly.
In image preview, Left/Right select adjacent images at fit size; zoom buttons
change magnification and arrows pan when zoomed. The app does not browse outside
the shared root or follow directory symlinks. Deleting a folder removes its
contents; there is no trash/undo. Unsupported formats and naming operations such
as rename/new folder remain available through the web file manager.

The standard release builder builds this app and its package. To rebuild only
its executable after the shared libraries have been built:

```sh
antix/apps/files/build.sh build/antix/ui
```

`test-navigation.cpp` exercises navigation, image zoom, text/details, live updates,
canceling deletion, and confirmed deletion of disposable files/folders offscreen.
It never targets the Reader's shared data.
