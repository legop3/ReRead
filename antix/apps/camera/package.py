"""Package this standalone ReRead app; publishing uses the shared release tool."""
from pathlib import Path
import shutil
import subprocess


def build(base, build_dir, output):
    ui = build_dir / 'ui'
    subprocess.run([str(base / 'antix/apps/camera/build.sh'), str(ui)], check=True)
    output.mkdir(parents=True, exist_ok=True)
    app_version = (base / 'antix/apps/camera/VERSION').read_text().strip()
    app = build_dir / 'reread-app-camera'
    if app.exists(): shutil.rmtree(app)
    def appwrite(path, content):
        dest = app / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(content)
    appwrite('DEBIAN/control', f"""Package: reread-app-camera
Version: {app_version}
Architecture: i386
Maintainer: ReRead contributors
Section: graphics
Priority: optional
Depends: reread (>= 0.21.0), libqt5network5t64
Recommends: reread-app-images
Description: ReRead camera
 Native button-operated camera with live preview and full-resolution photos.
""")
    appwrite('usr/share/applications/reread-camera.desktop', """[Desktop Entry]
Type=Application
Name=Camera
Comment=Live view, photographs and documents
Exec=/usr/lib/reread/apps/camera/reader-camera-app
Icon=reread-camera
Categories=Graphics;Photography;
X-ReRead=true
Terminal=false
""")
    appwrite('usr/share/reread/apps/reread-app-camera.json', '{"name":"Camera","description":"Live view, photographs and documents","desktop":"reread-camera.desktop"}\n')
    camera_icon = app / 'usr/share/icons/hicolor/scalable/apps/reread-camera.svg'
    camera_icon.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(base / 'antix/apps/camera/reread-camera.svg', camera_icon)
    target = app / 'usr/lib/reread/apps/camera/reader-camera-app'
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(ui / 'reader-camera-app', target)
    target.chmod(0o755)
    subprocess.run(['dpkg-deb', '--root-owner-group', '-Zgzip', '--build', str(app), str(output / f'reread-app-camera_{app_version}_i386.deb')], check=True)

    return output / ('reread-app-camera_' + (base / 'antix/apps/camera/VERSION').read_text().strip() + '_i386.deb')


if __name__ == '__main__':
    base = Path(__file__).resolve().parents[3]
    print(build(base, base / 'build/antix', base / 'out/antix'))
