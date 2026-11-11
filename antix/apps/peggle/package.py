#!/usr/bin/env python3
"""Build only the independent Peggle ISO importer .deb, like Booru."""
from pathlib import Path
import json
import os
import shutil
import subprocess

BASE = Path(__file__).resolve().parents[3]
SOURCE = Path(__file__).resolve().parent
BUILD = BASE / "build/antix/ui"
OUTPUT = BASE / "out/antix"


def main():
    version = (SOURCE / "VERSION").read_text().strip()
    subprocess.run(["dpkg", "--validate-version", version], check=True)
    BUILD.mkdir(parents=True, exist_ok=True)
    compiler_env = dict(os.environ, PKG_CONFIG_LIBDIR="/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig")

    def pkg(*names, mode="--cflags"):
        return subprocess.check_output(
            ["pkg-config", mode, *names], text=True, env=compiler_env
        ).split()

    ui = BASE / "antix/ui"
    device_flags = pkg("Qt5Core", "Qt5DBus")
    device_libs = pkg("Qt5Core", "Qt5DBus", mode="--libs")
    widget_flags = pkg("Qt5Widgets")
    widget_libs = pkg("Qt5Widgets", mode="--libs")

    # Exactly like Booru: build only the two SDK libraries this app uses.
    moc = BUILD / "moc-reader-device.cpp"
    subprocess.run(["moc", str(ui / "reader-device.hpp"), "-o", str(moc)], check=True)
    subprocess.run([
        "g++", "-m32", "-std=c++17", "-O2", "-fPIC",
        "-Wall", "-Wextra", "-Werror", *device_flags,
        "-I" + str(BASE / "antix"), "-shared",
        "-Wl,-soname,libreader-device.so.1",
        str(ui / "reader-device.cpp"), str(moc),
        *device_libs, "-o", str(BUILD / "libreader-device.so.1")
    ], check=True, env=compiler_env)
    subprocess.run(["ln", "-sfn", "libreader-device.so.1",
                    str(BUILD / "libreader-device.so")], check=True)
    subprocess.run([
        "g++", "-m32", "-std=c++17", "-O2", "-fPIC",
        "-Wall", "-Wextra", "-Werror", *widget_flags,
        "-I" + str(BASE / "antix"), "-shared",
        "-Wl,-soname,libreader-ui.so.5",
        str(ui / "reader-ui.cpp"), "-L" + str(BUILD),
        "-lreader-device", *widget_libs,
        "-o", str(BUILD / "libreader-ui.so.5")
    ], check=True, env=compiler_env)
    subprocess.run(["ln", "-sfn", "libreader-ui.so.5",
                    str(BUILD / "libreader-ui.so")], check=True)

    binary = BUILD / "reader-peggle-installer"
    subprocess.run([
        "g++", "-m32", "-std=c++17", "-O2", "-fPIC",
        "-Wall", "-Wextra", "-Werror", *widget_flags,
        "-I" + str(BASE / "antix/ui"), "-I" + str(BASE / "antix"),
        str(SOURCE / "main.cpp"), "-L" + str(BUILD),
        "-lreader-ui", "-lreader-device",
        *widget_libs, "-o", str(binary)
    ], check=True, env=compiler_env)

    stage = BASE / "build/antix/reread-app-peggle-installer"
    if stage.exists():
        shutil.rmtree(stage)

    def write(path, data):
        target = stage / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(data)

    write("DEBIAN/control", f"""Package: reread-app-peggle-installer
Version: {version}
Architecture: i386
Maintainer: ReRead contributors
Section: games
Priority: optional
Depends: reread (>= 0.20.0), python3, 7zip
Description: Peggle Deluxe retail ISO importer for ReRead
 Extracts an owner-supplied retail ISO without executing its installer.
 This package imports game data but does not run the Windows game.
""")
    write("usr/share/applications/reread-peggle-installer.desktop", """[Desktop Entry]
Type=Application
Name=Peggle installer
Comment=Import Peggle Deluxe files from an ISO
Exec=/usr/lib/reread/apps/peggle/reader-peggle-installer
TryExec=/usr/lib/reread/apps/peggle/reader-peggle-installer
Icon=reread-peggle-installer
Categories=Game;
X-ReRead=true
Terminal=false
""")
    write("usr/share/reread/apps/reread-app-peggle-installer.json", json.dumps({
        "name": "Peggle installer",
        "description": "Import Peggle Deluxe from a retail ISO",
        "desktop": "reread-peggle-installer.desktop"
    }) + "\n")
    write("usr/share/icons/hicolor/scalable/apps/reread-peggle-installer.svg",
          """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><circle cx="32" cy="32" r="24" fill="#f58220"/><circle cx="32" cy="32" r="9" fill="#fff"/><circle cx="32" cy="32" r="4" fill="#222"/></svg>\n""")

    target = stage / "usr/lib/reread/apps/peggle/reader-peggle-installer"
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(binary, target)
    target.chmod(0o755)
    importer = stage / "usr/lib/reread/apps/peggle/import-iso.py"
    shutil.copy(SOURCE / "import-iso.py", importer)
    importer.chmod(0o755)
    docs = stage / "usr/share/doc/reread-app-peggle-installer"
    docs.mkdir(parents=True, exist_ok=True)
    shutil.copy(SOURCE / "README.md", docs / "README.md")

    OUTPUT.mkdir(parents=True, exist_ok=True)
    package = OUTPUT / f"reread-app-peggle-installer_{version}_i386.deb"
    subprocess.run(["dpkg-deb", "--root-owner-group", "-Zgzip",
                    "--build", str(stage), str(package)], check=True)
    print(package)


if __name__ == "__main__":
    main()
