# ReRead 0.21 TODO completion (2026-11-10)

Core 0.21.0, Booru 0.5.0, Camera 0.9.0. UI SONAME remains 5.

- Built the changed Booru and Camera executables against the existing SDK; no kernel or installer rebuild.
- `antix/test-app-settings.py`: real JSON Schema validation, config persistence, rejected stale/invalid writes, installed-app discovery in disposable paths.
- `antix/apps/booru/test-opening.cpp`: entering a post leaves its card visible, repeated View does not launch, explicit View dispatches the cached sample through a real isolated GIO MIME handler.
- Installed the three packages on the Reader; `dpkg --audit` clean.
- `antix/test-web-app-settings.cjs` against the Reader: offline nested schema form discovered from installed Booru metadata, password input fields, save/reopen, rejected invalid/stale saves, no duplicate Reader toggle. Restored original account/search values in finally.
- `antix/test-web-files.cjs` against the Reader: folders, upload, existing UTF-8 text edited/saved/reloaded, normal tab navigation and shared storage. Deletes only its unique test folder. Fixed the test to wait for completed upload content rather than a newly created empty file.
- On-device Booru Options: simulated physical X keys toggled automatic opening on and off; private preferences persisted correctly. Left it off.
- Camera Last photo launched the installed Images MIME handler with `REREAD_SCREEN_ORIENTATION=180`. Framebuffer capture of its Guide showed correctly rotated controls at the physical top. Closed viewer and Camera normally.
- `xrandr --query` exposed a fixed 960x544 framebuffer output. `--output default --rotate inverted` was rejected: output cannot use rotation. No global X configuration changed; the optional per-launch Screen hint avoids a daemon or display ownership changes.

Browser test data and screenshots are not published. There was no new hardware suspend, optical-focus or audio test: none of those paths changed.
