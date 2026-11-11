#!/usr/bin/env python3
"""Package the ISO-import UI; intentionally does not depend on or install Wine."""
from pathlib import Path
import json
import shutil
import subprocess

base = Path(__file__).resolve().parents[3]
source = Path(__file__).resolve().parent
stage = base / "build/antix/reread-app-peggle-installer"
if stage.exists():
    shutil.rmtree(stage)

def write(name, data):
    path=stage/name
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(data)

write("DEBIAN/control", """Package: reread-app-peggle-installer
Version: 0.1.0
Architecture: i386
Maintainer: ReRead contributors
Section: games
Priority: optional
Depends: reread (>= 0.20.0), python3, 7zip
Description: Peggle Deluxe retail ISO importer for ReRead
 Extracts an owner-supplied retail ISO without running its Windows installer.
 This is an importer, not yet a playable Peggle runtime.
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
    "name":"Peggle installer",
    "description":"Import Peggle Deluxe from a retail ISO",
    "desktop":"reread-peggle-installer.desktop"})+"\n")
write("usr/share/icons/hicolor/scalable/apps/reread-peggle-installer.svg",
      """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><circle cx="32" cy="32" r="24" fill="#f58220"/><circle cx="32" cy="32" r="9" fill="#fff"/><circle cx="32" cy="32" r="4" fill="#222"/></svg>\n""")
for name in ("reader-peggle-installer",):
    out=stage/"usr/lib/reread/apps/peggle"/name
    out.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy(base/"build/antix/ui"/name,out)
    out.chmod(0o755)
out=stage/"usr/lib/reread/apps/peggle/import-iso.py"
shutil.copy(source/"import-iso.py",out)
out.chmod(0o755)
destination=base/"out/antix/reread-app-peggle-installer_0.1.0_i386.deb"
destination.parent.mkdir(parents=True,exist_ok=True)
subprocess.run(["dpkg-deb","--root-owner-group","-Zgzip","--build",str(stage),str(destination)],check=True)
print(destination)
