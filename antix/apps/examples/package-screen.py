"""Package the already-built UI starter; never included in default installs."""
from pathlib import Path
import json
import shutil
import subprocess

base = Path(__file__).resolve().parents[3]
root = base / 'build/app-example'
if root.exists():
    shutil.rmtree(root)

def write(path, value):
    target = root / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(value)

write('DEBIAN/control', '''Package: reread-app-example
Version: 0.2.0
Architecture: i386
Maintainer: ReRead contributors
Section: utils
Priority: optional
Depends: reread (>= 0.20.0)
Description: ReRead shared UI example
 Independent Qt application demonstrating collections and button-icon help.
''')
write('usr/share/applications/reread-example.desktop', '''[Desktop Entry]
Type=Application
Name=Example
Comment=Shared UI development example
Exec=/usr/lib/reread/apps/example/example
TryExec=/usr/lib/reread/apps/example/example
Icon=reread-example
Categories=Utility;
X-ReRead=true
Terminal=false
''')
write('usr/share/icons/hicolor/scalable/apps/reread-example.svg', '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><rect x="7" y="9" width="50" height="46" rx="3" fill="none" stroke="white" stroke-width="4"/><path d="M15 21h18m-18 11h18m-18 11h18M41 18v28" stroke="#00bfff" stroke-width="4"/></svg>\n''')
write('usr/share/reread/apps/reread-app-example.json', json.dumps({
    'name': 'Example', 'description': 'Shared UI development example',
    'desktop': 'reread-example.desktop'}) + '\n')
executable = root / 'usr/lib/reread/apps/example/example'
executable.parent.mkdir(parents=True, exist_ok=True)
shutil.copy(base / 'build/antix/ui/screen-window-example', executable)
executable.chmod(0o755)
output = base / 'out/examples/reread-app-example_0.2.0_i386.deb'
output.parent.mkdir(parents=True, exist_ok=True)
subprocess.run(['desktop-file-validate', str(root / 'usr/share/applications/reread-example.desktop')], check=True)
subprocess.run(['dpkg-deb', '--root-owner-group', '-Zgzip', '--build', str(root), str(output)], check=True)
print(output)
