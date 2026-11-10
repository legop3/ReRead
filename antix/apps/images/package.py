"""Build the standalone image handler using the already-built ReRead SDK."""
from pathlib import Path
import shutil
import subprocess


def build(base, build_dir, output):
    source = base / 'antix/apps/images'
    subprocess.run([str(source / 'build.sh'), str(build_dir / 'ui')], check=True)
    version = (source / 'VERSION').read_text().strip()
    root = build_dir / 'reread-app-images'
    if root.exists(): shutil.rmtree(root)
    def write(path, text):
        dest = root / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(text)
    def copy(path, target):
        dest = root / target
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(path, dest)
    write('DEBIAN/control', f'''Package: reread-app-images
Version: {version}
Architecture: i386
Maintainer: ReRead contributors
Section: graphics
Priority: optional
Depends: reread (>= 0.18.0), qt5-image-formats-plugins, desktop-file-utils
Description: ReRead image viewer
 Standalone MIME image handler with zoom, pan, rotation and Reader controls.
''')
    copy(build_dir / 'ui/reader-images-app', 'usr/lib/reread/apps/images/reader-images-app')
    copy(source / 'reread-images.desktop', 'usr/share/applications/reread-images.desktop')
    copy(source / 'reread-images.svg', 'usr/share/icons/hicolor/scalable/apps/reread-images.svg')
    copy(source / 'README.md', 'usr/share/doc/reread-app-images/README.md')
    write('usr/share/reread/apps/reread-app-images.json', '{"name":"Images","description":"Default image viewer","desktop":"reread-images.desktop"}\n')
    output.mkdir(parents=True, exist_ok=True)
    package = output / f'reread-app-images_{version}_i386.deb'
    subprocess.run(['dpkg-deb', '--root-owner-group', '-Zgzip', '--build', str(root), str(package)], check=True)
    return package


if __name__ == '__main__':
    base = Path(__file__).resolve().parents[3]
    print(build(base, base / 'build/antix', base / 'out/antix'))
