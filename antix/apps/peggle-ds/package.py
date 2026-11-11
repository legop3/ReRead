#!/usr/bin/env python3
"""Build only the ReRead Peggle DS app and its SDL2 input adapter."""
from pathlib import Path
import json
import os
import shutil
import subprocess

BASE=Path(__file__).resolve().parents[3]
SOURCE=Path(__file__).resolve().parent
BUILD=BASE/"build/antix/ui"
OUTPUT=BASE/"out/antix"

def main():
    version=(SOURCE/"VERSION").read_text().strip()
    subprocess.run(["dpkg","--validate-version",version],check=True)
    BUILD.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,PKG_CONFIG_LIBDIR="/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig")
    def pkg(*names, mode="--cflags"):
        return subprocess.check_output(["pkg-config",mode,*names],text=True,env=env).split()
    ui=BASE/"antix/ui"
    device_flags=pkg("Qt5Core","Qt5DBus")
    device_libs=pkg("Qt5Core","Qt5DBus",mode="--libs")
    widget_flags=pkg("Qt5Widgets")
    widget_libs=pkg("Qt5Widgets",mode="--libs")
    moc=BUILD/"moc-reader-device.cpp"
    subprocess.run(["moc",str(ui/"reader-device.hpp"),"-o",str(moc)],check=True)
    subprocess.run(["g++","-m32","-std=c++17","-O2","-fPIC","-Wall","-Wextra","-Werror",
                    *device_flags,"-I"+str(BASE/"antix"),"-shared",
                    "-Wl,-soname,libreader-device.so.1",str(ui/"reader-device.cpp"),
                    str(moc),*device_libs,"-o",str(BUILD/"libreader-device.so.1")],check=True,env=env)
    subprocess.run(["ln","-sfn","libreader-device.so.1",str(BUILD/"libreader-device.so")],check=True)
    subprocess.run(["g++","-m32","-std=c++17","-O2","-fPIC","-Wall","-Wextra","-Werror",
                    *widget_flags,"-I"+str(BASE/"antix"),"-shared",
                    "-Wl,-soname,libreader-ui.so.5",str(ui/"reader-ui.cpp"),
                    "-L"+str(BUILD),"-lreader-device",*widget_libs,
                    "-o",str(BUILD/"libreader-ui.so.5")],check=True,env=env)
    subprocess.run(["ln","-sfn","libreader-ui.so.5",str(BUILD/"libreader-ui.so")],check=True)
    binary=BUILD/"reader-peggle-ds"
    subprocess.run(["g++","-m32","-std=c++17","-O2","-fPIC","-Wall","-Wextra","-Werror",
                    *widget_flags,"-I"+str(ui),"-I"+str(BASE/"antix"),str(SOURCE/"main.cpp"),
                    "-L"+str(BUILD),"-lreader-ui","-lreader-device",*widget_libs,
                    "-o",str(binary)],check=True,env=env)
    shim=BUILD/"sdl-reader.so"
    subprocess.run(["gcc","-m32","-std=c11","-O2","-fPIC","-shared",
                    "-Wall","-Wextra","-Werror",str(SOURCE/"sdl-reader.c"),
                    "-ldl","-o",str(shim)],check=True,env=env)
    stage=BASE/"build/antix/reread-app-peggle-ds"
    if stage.exists():shutil.rmtree(stage)
    def write(name,data):
        target=stage/name
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text(data)
    write("DEBIAN/control",f"""Package: reread-app-peggle-ds
Version: {version}
Architecture: i386
Maintainer: ReRead contributors
Section: games
Priority: optional
Depends: reread (>= 0.20.0), desmume
Description: Nintendo DS Peggle launcher for ReRead
 Run owner-supplied Nintendo DS games with button-only controls and
 software-rendered DeSmuME. No ROM is included.
""")
    write("usr/share/applications/reread-peggle-ds.desktop","""[Desktop Entry]
Type=Application
Name=Peggle DS
Comment=Play Peggle Dual Shot with ReRead buttons
Exec=/usr/lib/reread/apps/peggle-ds/reader-peggle-ds
TryExec=/usr/lib/reread/apps/peggle-ds/reader-peggle-ds
Icon=reread-peggle-ds
Categories=Game;
X-ReRead=true
Terminal=false
""")
    write("usr/share/icons/hicolor/scalable/apps/reread-peggle-ds.svg",
          """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><rect width="64" height="64" rx="10" fill="#111"/><circle cx="23" cy="38" r="13" fill="#f58220"/><circle cx="42" cy="20" r="9" fill="#ffdb52"/><circle cx="47" cy="44" r="6" fill="#38bdf8"/></svg>\n""")
    write("usr/share/reread/apps/reread-app-peggle-ds.json",json.dumps({
      "name":"Peggle DS","description":"Play Peggle: Dual Shot with Nintendo DS emulation",
      "desktop":"reread-peggle-ds.desktop"
    })+"\n")
    dest=stage/"usr/lib/reread/apps/peggle-ds"
    dest.mkdir(parents=True,exist_ok=True)
    for src,name in ((binary,"reader-peggle-ds"),(shim,"sdl-reader.so")):
        shutil.copy(src,dest/name)
    (dest/"reader-peggle-ds").chmod(0o755)
    docs=stage/"usr/share/doc/reread-app-peggle-ds"
    docs.mkdir(parents=True,exist_ok=True)
    shutil.copy(SOURCE/"README.md",docs/"README.md")
    OUTPUT.mkdir(parents=True,exist_ok=True)
    result=OUTPUT/f"reread-app-peggle-ds_{version}_i386.deb"
    subprocess.run(["dpkg-deb","--root-owner-group","-Zgzip","--build",str(stage),str(result)],check=True)
    print(result)

if __name__=="__main__":
    main()
