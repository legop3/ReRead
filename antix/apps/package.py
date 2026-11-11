"""Shared app build entry point; app-specific packagers own their payloads."""
from pathlib import Path
import argparse
import shutil
import runpy
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
        recipe = runpy.run_path(str(packager))
        if callable(recipe.get('build')):
            recipe['build'](base, build_dir, output)
        else:
            subprocess.run(['python3', str(packager)], check=True)
    else:
        raise ValueError('App has no package.py recipe: ' + name)
    packages = [output / f'reread-app-{name}_{version}_{arch}.deb' for arch in ('i386', 'all')]
    packages = [path for path in packages if path.is_file()]
    if len(packages) != 1:
        raise RuntimeError('Packager must produce exactly one i386/all package for ' + name + ' ' + version)
    return packages[0]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', help='Directory name under antix/apps')
    args = parser.parse_args()
    base = Path(__file__).resolve().parents[2]
    subprocess.run([str(base / 'antix/ui/build.sh'), str(base / 'build/antix/ui')], check=True)
    print(build(args.app, base, base / 'build/antix', base / 'out/antix'))
