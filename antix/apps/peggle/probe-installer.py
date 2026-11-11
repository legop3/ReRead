#!/usr/bin/env python3
"""Probe the actual Peggle installer embedded in the retail ISO.

Extracts only installers/PeggleSetup.exe into a temporary directory, lists its
archive entries if recognized, and reports whether a game executable appears.
Does not execute the installer, download anything, or modify the Reader.
"""
from pathlib import Path
import argparse
import json
import re
import shutil
import subprocess
import tempfile


def entries(text):
    result = []
    for block in re.split(r"\n\s*\n", text):
        fields = dict(re.findall(r"^([^=\n]+) = (.*)$", block, re.M))
        if "Path" in fields and "Size" in fields and fields.get("Folder") != "+":
            try:
                size = int(fields["Size"])
            except ValueError:
                continue
            result.append({"path": fields["Path"], "bytes": size})
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("iso", type=Path)
    a = p.parse_args()
    iso = a.iso.resolve()
    if not iso.is_file():
        p.error(f"ISO not found: {iso}")
    if not shutil.which("7z"):
        p.error("7z is required")
    with tempfile.TemporaryDirectory(prefix="reread-peggle-") as tmp:
        target = Path(tmp)
        subprocess.run(
            ["7z", "e", "-y", f"-o{target}", str(iso), "installers/PeggleSetup.exe"],
            check=True, stdout=subprocess.DEVNULL)
        setup = target / "PeggleSetup.exe"
        if not setup.is_file():
            p.error("ISO did not contain installers/PeggleSetup.exe")
        probe = subprocess.run(["7z", "l", "-slt", str(setup)],
                               capture_output=True, text=True)
        if probe.returncode:
            print(json.dumps({
                "installer_bytes": setup.stat().st_size,
                "archive_readable": False,
                "error": (probe.stderr or probe.stdout)[-1500:]
            }, indent=2))
            return
        files = entries(probe.stdout)
        game = [f for f in files if Path(f["path"].replace("\\", "/")).name.lower() == "peggle.exe"]
        print(json.dumps({
            "installer_bytes": setup.stat().st_size,
            "archive_readable": True,
            "archive_type": next((m.group(1) for line in probe.stdout.splitlines()
                   if (m := re.match(r"^Type = (.+)$", line))), "unknown"),
            "entry_count": len(files),
            "total_unpacked_bytes": sum(f["bytes"] for f in files),
            "game_executables": game,
            "largest_entries": sorted(files, key=lambda f: f["bytes"], reverse=True)[:25]
        }, indent=2))


if __name__ == "__main__":
    main()
