# Peggle Deluxe for ReRead (experimental)

The original PopCap Peggle Deluxe 1.0 Rev 3 CD contains a directly extractable
game payload. ReRead does not need to retain the ISO or run its Windows setup.

## Import from the Reader

Upload `peggle_retail_na.iso` into ReRead Files using its USB web UI. The
on-device import tool can then be invoked by an app UI (not implemented yet);
for initial testing, use a Reader terminal:

```sh
python3 antix/apps/peggle/import-iso.py /var/lib/reader/files/peggle_retail_na.iso --delete-iso
```

The script retains the ISO on any failed import, extracts only the installer and
Peggle files, verifies the game executable, data pack, music and DLLs, then
atomically moves them into `/var/lib/reader/apps/peggle/game`. It removes the
source ISO *only when requested* and only after success. It never executes the
installer. The installed game is expected to require about 17 MiB. In-progress
extraction requires at least 65 MiB free in addition to the uploaded ISO.

## Still needed

This is the **import layer**, not a playable app. Before installing Wine, test
its actual installed footprint on antiX i386. Debian 13's 32-bit libwine alone
is ~609 MiB. Wine graphics, audio, input-to-mouse mapping, and a GUI install
button still require implementation and testing on the Reader. A safe default
is not to download or install Wine until its feasibility has been verified.

Game content must be supplied by the device owner; no copyrighted game binaries
are distributed with this source.
