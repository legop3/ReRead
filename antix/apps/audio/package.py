"""Build the standalone audio handler using the already-built ReRead SDK."""
from pathlib import Path
import shutil
import subprocess


def build(base, build_dir, output):
    source = base / 'antix/apps/audio'
    subprocess.run([str(source / 'build.sh'), str(build_dir / 'ui')], check=True)
    version = (source / 'VERSION').read_text().strip()
    root = build_dir / 'reread-app-audio'
    if root.exists(): shutil.rmtree(root)
    def write(path, text):
        dest = root / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(text)
    def copy(path, target):
        dest = root / target
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(path, dest)
    write('DEBIAN/control', f'''Package: reread-app-audio
Version: {version}
Architecture: i386
Maintainer: ReRead contributors
Section: sound
Priority: optional
Depends: reread (>= 0.19.0), desktop-file-utils, libgstreamer1.0-0, gstreamer1.0-plugins-base, gstreamer1.0-plugins-good, gstreamer1.0-alsa
Description: ReRead audio player
 MIME audio handler using GStreamer and ALSA for files and HTTP streams.
''')
    copy(build_dir / 'ui/reader-audio-app', 'usr/lib/reread/apps/audio/reader-audio-app')
    copy(source / 'reread-audio.desktop', 'usr/share/applications/reread-audio.desktop')
    copy(source / 'reread-audio.svg', 'usr/share/icons/hicolor/scalable/apps/reread-audio.svg')
    copy(source / 'README.md', 'usr/share/doc/reread-app-audio/README.md')
    write('usr/share/reread/apps/reread-app-audio.json', '{"name":"Audio","description":"Default audio player","desktop":"reread-audio.desktop"}\n')
    output.mkdir(parents=True, exist_ok=True)
    package = output / f'reread-app-audio_{version}_i386.deb'
    subprocess.run(['dpkg-deb', '--root-owner-group', '-Zgzip', '--build', str(root), str(package)], check=True)
    return package


if __name__ == '__main__':
    base = Path(__file__).resolve().parents[3]
    print(build(base, base / 'build/antix', base / 'out/antix'))
