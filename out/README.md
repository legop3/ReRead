Generated ReRead deliverables are in `antix/`:

- `reread_<version>_i386.deb`: in-place platform update.
- `reread-app-camera_<version>_i386.deb`: independently installable Camera app.
- `reread-installer-usb.img`: automatic USB installer (replaces the Reader SSD).
- `reread.img.gz`: compressed SSD payload used by that installer.
- `manifest.json`: exact platform/app packages and image layout for the build.

See [build, installation and update instructions](../antix/README.md).
