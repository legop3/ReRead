"""Shared app build entry point; app-specific packagers own their payloads."""
from pathlib import Path
import argparse
import shutil
import subprocess

DEFAULT_APPS = ('camera', 'software', 'files', 'images', 'text', 'audio')


def build(name, base, build_dir, output):
    source = base / 'antix/apps' / name
    if not (source / 'VERSION').is_file():
        raise ValueError('Unknown app: ' + name)
    version = (source / 'VERSION').read_text().strip()
    subprocess.run(['dpkg', '--validate-version', version], check=True)
    output.mkdir(parents=True, exist_ok=True)
    if not (build_dir / 'ui/libreader-ui.so').exists():
        subprocess.run([str(base / 'antix/ui/build.sh'), str(build_dir / 'ui')], check=True)
    packager = source / 'package.py'
    if packager.exists():
        # Existing app recipes can keep their specific build dependencies/assets.
        subprocess.run(['python3', str(packager)], check=True)
    else:
        subprocess.run([str(source / 'build.sh'), str(build_dir / 'ui')], check=True)
        recipes = {'camera': camera, 'software': software, 'files': files}
        recipes[name](base, build_dir, output)
    package = output / f'reread-app-{name}_{version}_i386.deb'
    if not package.is_file():
        raise RuntimeError('Packager did not produce ' + str(package))
    return package


def camera(base, build_dir, output):
    ui = build_dir / 'ui'
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


def software(base, build_dir, output):
    ui = build_dir / 'ui'
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


def files(base, build_dir, output):
    ui = build_dir / 'ui'
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


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', help='Directory name under antix/apps')
    args = parser.parse_args()
    base = Path(__file__).resolve().parents[2]
    subprocess.run([str(base / 'antix/ui/build.sh'), str(base / 'build/antix/ui')], check=True)
    print(build(args.app, base, base / 'build/antix', base / 'out/antix'))
