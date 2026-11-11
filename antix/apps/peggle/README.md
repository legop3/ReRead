# Peggle Deluxe ISO importer for ReRead (experimental)

This app imports the original Peggle Deluxe 1.0 Rev 3 retail ISO without
executing the Windows installer. **It is not yet a playable game.** It does
not install Wine or any other large runtime dependency.

## Build the independent importer app

On a Fedora development computer with Docker and a ReRead checkout:

```sh
docker build -t reread-app-builder image-builder
docker run --rm -v "$PWD:/work" reread-app-builder sh -c '
  antix/ui/build.sh build/antix/ui &&
  antix/apps/peggle/build.sh build/antix/ui &&
  python3 antix/apps/peggle/package.py
'
```

Output: `out/antix/reread-app-peggle-installer_0.1.0_i386.deb`.

Install it via the ReRead web **Apps** page. The package depends on `7zip`
(available from Debian's i386 repository), but intentionally does **not**
install Wine.

## On-device import

1. Upload `peggle_retail_na.iso` to **Files** in ReRead's USB web UI.
2. Open **Peggle installer** from Home.
3. Select the ISO (Up/Down if there are multiple); press OK.
4. On success, the game files are installed under
   `/var/lib/reader/apps/peggle/game`, and the imported ISO is removed.
   On error, the ISO is kept so it can be retried.
5. Options rescans Files; Back returns Home while no import is running.

The program temporarily extracts only `installers/PeggleSetup.exe`, then
the game content from `files/Peggle Deluxe/`. It verifies `Peggle.exe`,
`main.pak`, required DLLs and music before installing. Other PopCap games,
DirectX installation media, setup leftovers and uninstallers are not retained.
In addition to the uploaded 344 MiB ISO, at least 65 MiB free is required for
temporary extraction. The permanent game payload is expected to be roughly
17 MiB. It deletes the ISO only after successful verification.

For testing the extraction on a Linux computer without the UI:

```sh
python3 antix/apps/peggle/import-iso.py /path/to/peggle_retail_na.iso \
  --dest /tmp/test-peggle-game
```

(The ISO remains unless `--delete-iso` is explicitly supplied.)

## Remaining game-launch work

This is **only the import layer**. Before installing Wine, measure its disk and
RAM footprint on the actual antiX i386 image; Debian 13's `libwine` i386
package alone occupies about 609 MiB. Investigate native DirectDraw behavior,
fullscreen or letterboxing, ALSA audio, and ReRead button-to-mouse input while
Wine owns the focused game window. Do not blindly install the full Wine suite.

No copyrighted Peggle game binaries are committed or distributed with this app.
