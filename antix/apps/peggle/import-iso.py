#!/usr/bin/env python3
"""Import only Peggle Deluxe from the known PopCap retail disc.

Runs on the Reader; never executes the Windows setup program.
The uploaded ISO is deleted ONLY after verified installation, and only with
--delete-iso. Installer and extraction intermediates live beside the destination,
so filesystem replacement is atomic. Failed imports retain the source ISO.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

GAME_FILES = (
    "Peggle.exe",
    "cdprops.xml",
    "main.pak",
    "bass.dll",
    "j2k-codec.dll",
    "music/PeggleBeats.mo3",
    "music/odetojoy.ogg",
    "music/opening.ogg",
)
# These are conservative minimum byte sizes, not expected exact hashes.
MIN_SIZES = {"Peggle.exe": 1_000_000, "main.pak": 5_000_000}
SETUP_PATH = "installers/PeggleSetup.exe"
CDPROPS_PATH = "installers/cdprops.xml"
GAME_FOLDER = "files/Peggle Deluxe"


def sevenzip(*args, stdout=None):
    subprocess.run(["7z", *map(str, args)], stdout=stdout, check=True)


def verify(folder):
    for filename in GAME_FILES:
        p = folder / filename
        if not p.is_file() or p.is_symlink() or p.stat().st_size < MIN_SIZES.get(filename, 1):
            raise RuntimeError(f"Missing or invalid required game file: {filename}")
    return sum(f.stat().st_size for f in folder.rglob("*") if f.is_file())


def import_game(iso, dest, delete_iso):
    if not shutil.which("7z"):
        raise RuntimeError("The Debian 7zip package is required")
    if not iso.is_file() or iso.is_symlink():
        raise RuntimeError("Expected a regular ISO file")
    if not dest.parent.is_dir():
        dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists():
        raise RuntimeError(f"Game already installed: {dest}")
    # ISO size + <=15 MB intermediate installer + <=32 MB extracted payload,
    # plus headroom for metadata and filesystem variations.
    free = shutil.disk_usage(dest.parent).free
    if free < 65 * 1024 * 1024:
        raise RuntimeError("At least 65 MiB free is required for extraction")
    with tempfile.TemporaryDirectory(prefix=".peggle-import-", dir=dest.parent) as td:
        staging = Path(td)
        setup_dir = staging / "setup"
        setup_dir.mkdir()
        print("Extracting Peggle installer from the ISO", flush=True)
        sevenzip("e", "-y", "-bd", "-bso0", "-bsp0", f"-o{setup_dir}", iso, SETUP_PATH)
        setup = setup_dir / "PeggleSetup.exe"
        if not setup.is_file() or not 10_000_000 <= setup.stat().st_size <= 20_000_000:
            raise RuntimeError("Unexpected PeggleSetup.exe; check the disc revision")
        print("Extracting Peggle game files", flush=True)
        unpack = staging / "unpack"
        unpack.mkdir()
        sevenzip("x", "-y", "-bd", "-bso0", "-bsp0", f"-o{unpack}", setup, f"{GAME_FOLDER}/*")
        source = unpack / GAME_FOLDER
        # PopCap retail builds look for cdprops.xml beside Peggle.exe.
        # It lives on the disc, outside the setup archive.
        print("Extracting retail CD properties", flush=True)
        sevenzip("e", "-y", "-bd", "-bso0", "-bsp0",
                 f"-o{source}", iso, CDPROPS_PATH)
        size = verify(source)
        if size > 40 * 1024 * 1024:
            raise RuntimeError("Unexpected game payload size")
        # Don't include uninstaller, bundled trial games or setup artifacts.
        for item in source.iterdir():
            if item.name not in {"Peggle.exe", "cdprops.xml", "main.pak", "bass.dll",
                                 "j2k-codec.dll", "music", "properties"}:
                if item.is_dir():
                    shutil.rmtree(item)
                else:
                    item.unlink()
        verify(source)
        # Refuse symlinks in recursively extracted game files.
        if any(f.is_symlink() for f in source.rglob("*")):
            raise RuntimeError("Refusing a symlink in extracted game files")
        source.rename(dest)
        print(f"Installed to {dest} ({size / 1048576:.1f} MiB extracted)", flush=True)
    if delete_iso:
        iso.unlink()
        print(f"Removed source ISO: {iso}", flush=True)
    else:
        print("Source ISO retained; use --delete-iso to remove after successful import", flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("iso", type=Path, help="Uploaded PopCap retail ISO")
    p.add_argument("--dest", type=Path, default=Path("/var/lib/reader/apps/peggle/game"))
    p.add_argument("--delete-iso", action="store_true",
                   help="Delete the supplied ISO only after successful verification")
    a = p.parse_args()
    try:
        import_game(a.iso.resolve(), a.dest.absolute(), a.delete_iso)
    except (RuntimeError, OSError, subprocess.CalledProcessError) as e:
        print(f"Peggle import failed: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
