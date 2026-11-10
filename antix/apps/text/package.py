"""Build the standalone text handler using the already-built ReRead SDK."""
from pathlib import Path
import shutil
import subprocess


def build(base, build_dir, output):
    source = base / 'antix/apps/text'
    subprocess.run([str(source / 'build.sh'), str(build_dir / 'ui')], check=True)
    version = (source / 'VERSION').read_text().strip()
    root = build_dir / 'reread-app-text'
    if root.exists(): shutil.rmtree(root)
    def write(path, text):
        dest = root / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(text)
    def copy(path, target):
        dest = root / target
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(path, dest)
    write('DEBIAN/control', f'''Package: reread-app-text
Version: {version}
Architecture: i386
Maintainer: ReRead contributors
Section: text
Priority: optional
Depends: reread (>= 0.20.0), desktop-file-utils
Description: ReRead text viewer
 Read-only MIME text handler with Unicode, wrapping and Reader controls.
''')
    copy(build_dir / 'ui/reader-text-app', 'usr/lib/reread/apps/text/reader-text-app')
    copy(source / 'reread-text.desktop', 'usr/share/applications/reread-text.desktop')
    copy(source / 'reread-text.svg', 'usr/share/icons/hicolor/scalable/apps/reread-text.svg')
    copy(source / 'README.md', 'usr/share/doc/reread-app-text/README.md')
    write('usr/share/reread/apps/reread-app-text.json', '{"name":"Text","description":"Default text viewer","desktop":"reread-text.desktop"}\n')
    output.mkdir(parents=True, exist_ok=True)
    package = output / f'reread-app-text_{version}_i386.deb'
    subprocess.run(['dpkg-deb', '--root-owner-group', '-Zgzip', '--build', str(root), str(package)], check=True)
    return package


if __name__ == '__main__':
    base = Path(__file__).resolve().parents[3]
    print(build(base, base / 'build/antix', base / 'out/antix'))
