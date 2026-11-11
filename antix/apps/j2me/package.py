#!/usr/bin/env python3
"""Build the independent Java ME runner with a locally supplied FreeJ2ME-Plus JAR."""
from pathlib import Path
import shutil
import subprocess
import argparse
import base64

BASE = Path(__file__).resolve().parents[3]
SOURCE = Path(__file__).resolve().parent
BUILD = BASE / "build/antix/ui"
OUTPUT = BASE / "out/antix"

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--emulator", type=Path, required=True, help="FreeJ2ME-Plus AWT JAR built on Fedora")
    args = parser.parse_args()
    if not args.emulator.is_file(): parser.error("FreeJ2ME-Plus JAR not found: "+str(args.emulator))
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
    binary = BUILD / "reader-j2me-app"
    flags = pkg("Qt5Widgets")
    libs = pkg("Qt5Widgets", mode="--libs")
    subprocess.run(["g++", "-m32", "-std=c++17", "-O2", "-fPIC", "-Wall", "-Wextra", "-Werror",
                    *flags, "-I" + str(BASE / "antix/ui"), "-I" + str(BASE / "antix"),
                    str(SOURCE / "main.cpp"), "-L" + str(BUILD), "-lreader-ui", "-lreader-device",
                    *libs, "-o", str(binary)], check=True, env=compiler_env)
    stage = BASE / "build/antix/reread-app-j2me"
    if stage.exists(): shutil.rmtree(stage)
    def write(path, data):
        target=stage/path
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text(data)
    write("DEBIAN/control", f"""Package: reread-app-j2me
Version: {version}
Architecture: i386
Maintainer: ReRead contributors
Section: games
Priority: optional
Depends: reread (>= 0.21.0), openjdk-21-jre
Description: General Java ME game runner for ReRead
 Play user-supplied mobile JAR games through FreeJ2ME-Plus.
 Includes per-game virtual display settings and isolated saves.
""")
    write("usr/share/applications/reread-j2me.desktop", """[Desktop Entry]
Type=Application
Name=Java ME
Comment=Run Java mobile games
Exec=/usr/lib/reread/apps/j2me/reader-j2me-app
TryExec=/usr/lib/reread/apps/j2me/reader-j2me-app
Icon=reread-j2me
Categories=Game;
X-ReRead=true
Terminal=false
""")
    write("usr/share/reread/apps/reread-app-j2me.json", '{"name":"Java ME","description":"Run classic mobile Java games","desktop":"reread-j2me.desktop"}\n')
    write("usr/share/icons/hicolor/scalable/apps/reread-j2me.svg", """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><rect width="64" height="64" rx="11" fill="#1e293b"/><rect x="15" y="5" width="34" height="54" rx="5" fill="#f8fafc"/><rect x="19" y="10" width="26" height="31" fill="#67e8f9"/><circle cx="32" cy="49" r="4" fill="#334155"/></svg>\\n""")
    docs=stage/"usr/share/doc/reread-app-j2me"
    docs.mkdir(parents=True,exist_ok=True)
    shutil.copy(SOURCE/"README.md",docs/"README.md")
    target=stage/"usr/lib/reread/apps/j2me/reader-j2me-app"
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy(binary,target)
    target.chmod(0o755)
    shutil.copy(args.emulator,target.parent/"freej2me_plus.jar")
    classes=target.parent/"classes"
    classes.mkdir(parents=True,exist_ok=True)
    for name in ("ReReadJ2ME.class","ReReadJ2ME$1.class"):
        (classes/name).write_bytes(base64.b64decode((SOURCE/(name+".b64")).read_text()))
    OUTPUT.mkdir(parents=True,exist_ok=True)
    package=OUTPUT/f"reread-app-j2me_{version}_i386.deb"
    subprocess.run(["dpkg-deb","--root-owner-group","-Zgzip","--build",str(stage),str(package)],check=True)
    print(package)
if __name__ == "__main__":
    main()
