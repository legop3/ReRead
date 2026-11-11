#!/usr/bin/env python3
"""Read J2ME game metadata without executing untrusted JAR contents."""
import json
import os
import sys
import zipfile

def game_info(path):
    info = {"path": path, "title": os.path.splitext(os.path.basename(path))[0],
            "vendor": "", "version": "", "valid": False}
    try:
        with zipfile.ZipFile(path) as archive:
            manifest = next((name for name in archive.namelist()
                             if name.upper() == "META-INF/MANIFEST.MF"), None)
            if not manifest:
                return info
            raw = archive.open(manifest).read(65536).decode("utf-8-sig", "replace")
            # Manifest continuation lines start with a single space.
            lines = []
            for line in raw.splitlines():
                if line.startswith(" ") and lines:
                    lines[-1] += line[1:]
                else:
                    lines.append(line)
            attrs = {}
            for line in lines:
                key, delim, value = line.partition(":")
                if delim:
                    attrs[key.strip().lower()] = value.strip()
            title = attrs.get("midlet-name", "")
            if not title:
                midlet = attrs.get("midlet-1", "")
                title = midlet.split(",")[0].strip()
            info.update(title=(title or info["title"])[:150],
                        vendor=attrs.get("midlet-vendor", "")[:150],
                        version=attrs.get("midlet-version", "")[:50],
                        valid=("midlet-1" in attrs or "midlet-name" in attrs))
    except (OSError, zipfile.BadZipFile, RuntimeError, ValueError):
        pass
    return info

if __name__ == "__main__":
    folder = sys.argv[1] if len(sys.argv) > 1 else "/var/lib/reader/files/J2ME"
    games = []
    for root, dirs, files in os.walk(folder):
        dirs[:] = [d for d in dirs if not d.startswith(".")]
        for name in sorted(files, key=str.casefold):
            if name.lower().endswith(".jar"):
                games.append(game_info(os.path.join(root, name)))
            if len(games) >= 500:
                break
        if len(games) >= 500:
            break
    print(json.dumps(games, ensure_ascii=False))
