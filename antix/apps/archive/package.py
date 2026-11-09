#!/usr/bin/env python3
"""Build only the independent Internet Archive app .deb, without a platform build."""
from pathlib import Path
import shutil
import subprocess

BASE = Path(__file__).resolve().parents[3]
SOURCE = Path(__file__).resolve().parent
BUILD = BASE / "build/antix/ui"
OUTPUT = BASE / "out/antix"

def main():
    version = (SOURCE / "VERSION").read_text().strip()
    subprocess.run(["dpkg", "--validate-version", version], check=True)
    if not (BUILD / "libreader-ui.so").exists():
        raise SystemExit("First run antix/ui/build.sh inside the reread-builder container.")
    env = {"PKG_CONFIG_LIBDIR": "/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig"}
    import os
    compiler_env = dict(os.environ, **env)
    binary = BUILD / "reader-archive-app"
    flags = subprocess.check_output(["pkg-config", "--cflags", "Qt5Widgets", "Qt5Network"], text=True, env=compiler_env).split()
    libs = subprocess.check_output(["pkg-config", "--libs", "Qt5Widgets", "Qt5Network"], text=True, env=compiler_env).split()
    subprocess.run(["g++", "-m32", "-std=c++17", "-O2", "-fPIC", "-Wall", "-Wextra", "-Werror",
                    *flags, "-I" + str(BASE / "antix/ui"), "-I" + str(BASE / "antix"),
                    str(SOURCE / "main.cpp"), "-L" + str(BUILD), "-lreader-ui", "-lreader-device",
                    *libs, "-o", str(binary)], check=True, env=compiler_env)
    stage = BASE / "build/antix/reread-app-archive"
    if stage.exists(): shutil.rmtree(stage)
    def write(path, data):
        target=stage/path
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text(data)
    write("DEBIAN/control", f"""Package: reread-app-archive
Version: {version}
Architecture: i386
Maintainer: ReRead contributors
Section: net
Priority: optional
Depends: reread (>= 0.13.0), libqt5network5t64, ffmpeg
Description: Internet Archive browser for ReRead
 Browse public media, texts and collections using physical controls.
 Stream content without storing downloaded media on the Reader.
""")
    write("usr/share/applications/reread-archive.desktop", """[Desktop Entry]
Type=Application
Name=Internet Archive
Comment=Browse and stream the Internet Archive
Exec=/usr/lib/reread/apps/archive/reader-archive-app
Icon=reread-archive
Categories=Network;AudioVideo;
X-ReRead=true
Terminal=false
""")
    write("usr/share/reread/apps/reread-app-archive.json", '{"name":"Internet Archive","description":"Browse and stream archived media","desktop":"reread-archive.desktop"}\n')
    icon=stage/"usr/share/icons/hicolor/scalable/apps/reread-archive.svg"
    icon.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy(SOURCE/"reread-archive.svg",icon)
    target=stage/"usr/lib/reread/apps/archive/reader-archive-app"
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy(binary,target)
    target.chmod(0o755)
    OUTPUT.mkdir(parents=True,exist_ok=True)
    package=OUTPUT/f"reread-app-archive_{version}_i386.deb"
    subprocess.run(["dpkg-deb","--root-owner-group","-Zgzip","--build",str(stage),str(package)],check=True)
    print(package)
if __name__ == "__main__":
    main()
