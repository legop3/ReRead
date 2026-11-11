#!/usr/bin/env python3
"""Inspect a Peggle retail ISO without executing its Windows binaries.

Requires p7zip-full on the build host. No files are installed on the Reader.
"""
import argparse
import json
import pathlib
import re
import shutil
import subprocess


def inspect(iso):
    listing = subprocess.check_output(
        ["7z", "l", "-slt", str(iso)], text=True
    )
    files = []
    for section in re.split(r"\n\s*\n", listing):
        fields = dict(re.findall(r"^([^=\n]+) = (.*)$", section, re.M))
        if "Path" not in fields or "Size" not in fields or fields.get("Folder") == "+":
            continue
        try:
            size = int(fields["Size"])
        except ValueError:
            continue
        if fields["Path"] == str(iso):
            continue
        files.append({"path": fields["Path"], "bytes": size})
    name = lambda f: pathlib.PurePosixPath(f["path"].replace("\\", "/")).name.lower()
    return {
        "iso_bytes": iso.stat().st_size,
        "file_count": len(files),
        "disc_files_bytes": sum(f["bytes"] for f in files),
        "installer_candidates": [f for f in files if re.search(
            r"(setup|install|peggle).*\.(exe|msi)$", name(f))],
        "game_executables": [f for f in files if name(f) == "peggle.exe"],
        "largest_files": sorted(files, key=lambda f: f["bytes"], reverse=True)[:25],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=pathlib.Path)
    parser.add_argument("--report", type=pathlib.Path)
    parser.add_argument("--extract", type=pathlib.Path)
    args = parser.parse_args()
    if not shutil.which("7z"):
        parser.error("Install p7zip-full on the build computer first")
    iso = args.iso.resolve()
    if not iso.is_file():
        parser.error(f"ISO not found: {iso}")
    report = inspect(iso)
    payload = json.dumps(report, indent=2) + "\n"
    print(payload, end="")
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(payload)
    if args.extract:
        target = args.extract.resolve()
        target.mkdir(parents=True, exist_ok=True)
        subprocess.run(["7z", "x", "-y", f"-o{target}", str(iso)], check=True)


if __name__ == "__main__":
    main()
