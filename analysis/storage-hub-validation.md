# Copyparty storage hub — ReRead 0.22.0

Uses native Copyparty volumes mapped to shared files, app data, caches, app
configuration, XDG preferences, installed programs and installed assets. The
last two are read-only; other app storage volumes retain ordinary file editing.
Overview cards and navigation use the supported Jinja html-head hook with CSP
nonces. No upstream code was changed and no app data was moved.

Validated:
- Real upstream HTTP/WebDAV transfer test with disposable filesystem mappings.
- Browser overview, cross-volume AJAX navigation, contextual physical paths,
  and packaged-program browsing against the same bundled Copyparty.
- Writable-volume upload/readback/delete; code/assets reject writes.
- Installed core 0.22.0 on the Reader; all seven volume listings return HTTP 200.
- Existing embedded-browser test on the Reader passes folder creation, upload,
  UTF-8 text editing/save/reload, management-tab navigation and shared-path display.
- dpkg --audit clean after installation. UI ABI remains 5; no app rebuilds.

The current published USB image remains 0.21.1; APT supplies this core update.
Fresh source builds include the new storage configuration.
