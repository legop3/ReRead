"""Package this standalone ReRead app; publishing uses the shared release tool."""
from pathlib import Path
import shutil
import subprocess


def build(base, build_dir, output):
    ui = build_dir / 'ui'
    subprocess.run([str(base / 'antix/apps/software/build.sh'), str(ui)], check=True)
    output.mkdir(parents=True, exist_ok=True)
    app = build_dir / 'reread-app-software'
    if app.exists(): shutil.rmtree(app)
    software_version = (base / 'antix/apps/software/VERSION').read_text().strip()
    def appwrite(path, content):
        dest = app / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(content)
    appwrite('DEBIAN/control', f"""Package: reread-app-software
Version: {software_version}
Architecture: i386
Maintainer: ReRead contributors
Section: admin
Priority: optional
Depends: reread (>= 0.20.0)
Description: ReRead Software manager
 Check and install system updates, browse applications, and install or remove apps.
""")
    appwrite('usr/share/applications/reread-software.desktop', """[Desktop Entry]
Type=Application
Name=Software
Comment=System updates and applications
Exec=/usr/lib/reread/apps/software/reader-software-app
Icon=reread-software
Categories=System;PackageManager;
X-ReRead=true
Terminal=false
""")
    appwrite('usr/share/reread/apps/reread-app-software.json', '{"name":"Software","description":"System updates and applications","desktop":"reread-software.desktop"}\n')
    software_icon = app / 'usr/share/icons/hicolor/scalable/apps/reread-software.svg'
    software_icon.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(base / 'antix/apps/software/reread-software.svg', software_icon)
    target = app / 'usr/lib/reread/apps/software/reader-software-app'
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(ui / 'reader-software-app', target)
    target.chmod(0o755)
    subprocess.run(['dpkg-deb', '--root-owner-group', '-Zgzip', '--build', str(app), str(output / f'reread-app-software_{software_version}_i386.deb')], check=True)

    return output / ('reread-app-software_' + (base / 'antix/apps/software/VERSION').read_text().strip() + '_i386.deb')


if __name__ == '__main__':
    base = Path(__file__).resolve().parents[3]
    print(build(base, base / 'build/antix', base / 'out/antix'))
