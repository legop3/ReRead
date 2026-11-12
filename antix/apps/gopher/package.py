#!/usr/bin/env python3
"""Build and package only the independent ReRead Gopher application."""
from pathlib import Path
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
    env = dict(os.environ, PKG_CONFIG_LIBDIR="/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig")
    def pkg(*names, mode="--cflags"):
        return subprocess.check_output(["pkg-config", mode, *names], text=True, env=env).split()
    flags = pkg("Qt5Widgets", "Qt5Network", "gio-2.0")
    libs = pkg("Qt5Widgets", "Qt5Network", "gio-2.0", mode="--libs")
    binary = BUILD / "reader-gopher-app"
    subprocess.run([
        "g++", "-m32", "-std=c++17", "-O2", "-fPIC", "-Wall", "-Wextra", "-Werror",
        *flags, "-I"+str(BASE / "antix/ui"), "-I"+str(BASE / "antix"),
        str(SOURCE / "main.cpp"), "-L"+str(BUILD),
        "-Wl,-rpath-link,"+str(BUILD), "-lreader-ui", "-lreader-device",
        *libs, "-o", str(binary)
    ], check=True, env=env)
    # Exercise protocol parsing and the current UI source on every app-only build.
    test_binary = BUILD / "reader-gopher-test"
    subprocess.run([
        "g++", "-m32", "-std=c++17", "-O2", "-fPIC", "-Wall", "-Wextra", "-Werror",
        *flags, "-I"+str(SOURCE), "-I"+str(BASE / "antix/ui"),
        "-I"+str(BASE / "antix"), str(SOURCE / "test-gopher.cpp"),
        "-L"+str(BUILD), "-Wl,-rpath-link,"+str(BUILD),
        "-lreader-ui", "-lreader-device", *libs, "-o", str(test_binary)
    ], check=True, env=env)
    subprocess.run([str(test_binary)], check=True,
                   env=dict(env, LD_LIBRARY_PATH=str(BUILD)))
    stage = BASE / "build/antix/reread-app-gopher"
    if stage.exists():
        shutil.rmtree(stage)
    def write(path, contents):
        target = stage / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(contents)
    write("DEBIAN/control", f"""Package: reread-app-gopher
Version: {version}
Architecture: i386
Maintainer: ReRead contributors
Section: net
Priority: optional
Depends: reread (>= 0.21.0), libqt5network5t64
Recommends: reread-app-images, reread-app-text, reread-app-audio
Description: Gopher browser for ReRead
 Explore Gopher menus and documents using the Reader's physical buttons.
 Save linked files and open them through installed MIME handlers.
""")
    write("usr/share/applications/reread-gopher.desktop", """[Desktop Entry]
Type=Application
Name=Gopher
Comment=Browse Gopherspace using physical controls
Exec=/usr/lib/reread/apps/gopher/reader-gopher-app
TryExec=/usr/lib/reread/apps/gopher/reader-gopher-app
Icon=reread-gopher
Categories=Network;
X-ReRead=true
Terminal=false
""")
    write("usr/share/reread/apps/reread-app-gopher.json",
          '{"name":"Gopher","description":"Explore Gopherspace","desktop":"reread-gopher.desktop"}\n')
    icon = stage / "usr/share/icons/hicolor/scalable/apps/reread-gopher.svg"
    icon.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(SOURCE / "reread-gopher.svg", icon)
    executable = stage / "usr/lib/reread/apps/gopher/reader-gopher-app"
    executable.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(binary, executable)
    executable.chmod(0o755)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    dest = OUTPUT / f"reread-app-gopher_{version}_i386.deb"
    subprocess.run(["desktop-file-validate", str(stage / "usr/share/applications/reread-gopher.desktop")],
                   check=True)
    subprocess.run(["dpkg-deb", "--root-owner-group", "-Zgzip", "--build", str(stage), str(dest)],
                   check=True)
    print(dest)

if __name__ == "__main__":
    main()
