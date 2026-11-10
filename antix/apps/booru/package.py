#!/usr/bin/env python3
"""Build only the independent Internet Booru app .deb, without a platform build."""
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
    BUILD.mkdir(parents=True, exist_ok=True)
    env = {"PKG_CONFIG_LIBDIR": "/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig"}
    import os
    compiler_env = dict(os.environ, **env)

    def pkg(*names, mode="--cflags"):
        return subprocess.check_output(["pkg-config", mode, *names], text=True, env=compiler_env).split()

    ui = BASE / "antix/ui"
    device_flags = pkg("Qt5Core", "Qt5DBus")
    device_libs = pkg("Qt5Core", "Qt5DBus", mode="--libs")
    widget_flags = pkg("Qt5Widgets")
    widget_libs = pkg("Qt5Widgets", mode="--libs")
    # Build only the libraries the app uses, not Home, Camera or the
    # session adapter. The full UI build also requires gio-unix-2.0.
    moc = BUILD / "moc-reader-device.cpp"
    subprocess.run(["moc", str(ui / "reader-device.hpp"), "-o", str(moc)], check=True)
    subprocess.run(["g++", "-m32", "-std=c++17", "-O2", "-fPIC",
                    "-Wall", "-Wextra", "-Werror", *device_flags,
                    "-I" + str(BASE / "antix"), "-shared",
                    "-Wl,-soname,libreader-device.so.1",
                    str(ui / "reader-device.cpp"), str(moc),
                    *device_libs, "-o", str(BUILD / "libreader-device.so.1")],
                   check=True, env=compiler_env)
    subprocess.run(["ln", "-sfn", "libreader-device.so.1",
                    str(BUILD / "libreader-device.so")], check=True)
    subprocess.run(["g++", "-m32", "-std=c++17", "-O2", "-fPIC",
                    "-Wall", "-Wextra", "-Werror", *widget_flags,
                    "-I" + str(BASE / "antix"), "-shared",
                    "-Wl,-soname,libreader-ui.so.5",
                    str(ui / "reader-ui.cpp"), "-L" + str(BUILD),
                    "-lreader-device", *widget_libs,
                    "-o", str(BUILD / "libreader-ui.so.5")],
                   check=True, env=compiler_env)
    subprocess.run(["ln", "-sfn", "libreader-ui.so.5",
                    str(BUILD / "libreader-ui.so")], check=True)
    binary = BUILD / "reader-booru-app"
    flags = pkg("Qt5Widgets", "Qt5Network", "gio-2.0")
    libs = pkg("Qt5Widgets", "Qt5Network", "gio-2.0", mode="--libs")
    subprocess.run(["g++", "-m32", "-std=c++17", "-O2", "-fPIC", "-Wall", "-Wextra", "-Werror",
                    *flags, "-I" + str(BASE / "antix/ui"), "-I" + str(BASE / "antix"),
                    str(SOURCE / "main.cpp"), "-L" + str(BUILD), "-lreader-ui", "-lreader-device",
                    *libs, "-o", str(binary)], check=True, env=compiler_env)
    stage = BASE / "build/antix/reread-app-booru"
    if stage.exists(): shutil.rmtree(stage)
    def write(path, data):
        target=stage/path
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text(data)
    write("DEBIAN/control", f"""Package: reread-app-booru
Version: {version}
Architecture: i386
Maintainer: ReRead contributors
Section: graphics
Priority: optional
Depends: reread (>= 0.21.0), libqt5network5t64
Recommends: reread-app-images
Description: Booru browser for ReRead
 Browse e926/e621-compatible galleries and manage account favorites with buttons.
 Open cached image samples through the default MIME handler; save originals explicitly.
""")
    write("usr/share/applications/reread-booru.desktop", """[Desktop Entry]
Type=Application
Name=Booru
Comment=Browse posts on compatible booru servers
Exec=/usr/lib/reread/apps/booru/reader-booru-app
TryExec=/usr/lib/reread/apps/booru/reader-booru-app
Icon=reread-booru
Categories=Network;Graphics;
X-ReRead=true
Terminal=false
""")
    write("usr/share/reread/apps/reread-app-booru.json", '{"name":"Booru","description":"Browse booru galleries","desktop":"reread-booru.desktop","settings":true}\n')
    write("etc/reread/apps/booru.json", (SOURCE/"config.example.json").read_text())
    write("usr/share/reread/apps/booru/settings.schema.json", (SOURCE/"settings.schema.json").read_text())
    write("DEBIAN/conffiles", "/etc/reread/apps/booru.json\n")
    icon=stage/"usr/share/icons/hicolor/scalable/apps/reread-booru.svg"
    icon.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy(SOURCE/"reread-booru.svg",icon)
    docs=stage/"usr/share/doc/reread-app-booru"
    docs.mkdir(parents=True,exist_ok=True)
    shutil.copy(SOURCE/"config.example.json",docs/"config.example.json")
    shutil.copy(SOURCE/"README.md",docs/"README.md")
    target=stage/"usr/lib/reread/apps/booru/reader-booru-app"
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy(binary,target)
    target.chmod(0o755)
    OUTPUT.mkdir(parents=True,exist_ok=True)
    package=OUTPUT/f"reread-app-booru_{version}_i386.deb"
    subprocess.run(["dpkg-deb","--root-owner-group","-Zgzip","--build",str(stage),str(package)],check=True)
    print(package)
if __name__ == "__main__":
    main()
