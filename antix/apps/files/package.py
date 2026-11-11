"""Package this standalone ReRead app; publishing uses the shared release tool."""
from pathlib import Path
import shutil
import subprocess


def build(base, build_dir, output):
    ui = build_dir / 'ui'
    subprocess.run([str(base / 'antix/apps/files/build.sh'), str(ui)], check=True)
    output.mkdir(parents=True, exist_ok=True)
    app = build_dir / 'reread-app-files'
    if app.exists(): shutil.rmtree(app)
    files_version = (base / 'antix/apps/files/VERSION').read_text().strip()
    def appwrite(path, content):
        dest = app / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(content)
    appwrite('DEBIAN/control', f"""Package: reread-app-files
Version: {files_version}
Architecture: i386
Maintainer: ReRead contributors
Section: utils
Priority: optional
Depends: reread (>= 0.20.0)
Recommends: reread-app-images, reread-app-text, reread-app-audio
Description: ReRead Files
 Browse shared folders, open files in default viewers, inspect and delete files.
""")
    appwrite('usr/share/applications/reread-files.desktop', """[Desktop Entry]
Type=Application
Name=Files
Comment=Browse folders, view and manage files
Exec=/usr/lib/reread/apps/files/reader-files-app
Icon=reread-files
Categories=Utility;FileManager;
X-ReRead=true
Terminal=false
""")
    appwrite('usr/share/reread/apps/reread-app-files.json', '{"name":"Files","description":"Browse folders, view and manage files","desktop":"reread-files.desktop"}\n')
    target = app / 'usr/share/icons/hicolor/scalable/apps/reread-files.svg'
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(base / 'antix/apps/files/reread-files.svg', target)
    target = app / 'usr/lib/reread/apps/files/reader-files-app'
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(ui / 'reader-files-app', target)
    target.chmod(0o755)
    subprocess.run(['dpkg-deb', '--root-owner-group', '-Zgzip', '--build', str(app), str(output / f'reread-app-files_{files_version}_i386.deb')], check=True)

    return output / ('reread-app-files_' + (base / 'antix/apps/files/VERSION').read_text().strip() + '_i386.deb')


if __name__ == '__main__':
    base = Path(__file__).resolve().parents[3]
    print(build(base, base / 'build/antix', base / 'out/antix'))
