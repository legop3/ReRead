#!/usr/bin/env python3
"""Build ReRead's static, signed APT archive using Debian's apt-ftparchive."""
import argparse
import gzip
import hashlib
import html
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

BASE = Path(__file__).resolve().parent
URL = 'https://legop3.github.io/ReRead/apt'


def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def build(packages, output, key, url=URL):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    archive = output / 'apt'
    pool = archive / 'pool/main'
    pool.mkdir(parents=True)
    names = set()
    for package in sorted(packages):
        fields = subprocess.check_output(
            ['dpkg-deb', '--show', '--showformat=${Package}\t${Version}\t${Architecture}', str(package)],
            text=True).split('\t')
        name, version, architecture = fields
        if not re.fullmatch(r'reread(?:-app-[a-z0-9][a-z0-9+.-]*)?', name) or architecture not in ('i386', 'all'):
            raise ValueError('Not a ReRead i386/all package: ' + str(package))
        destination = pool / f'{name}_{version}_{architecture}.deb'
        if destination.exists():
            if hashlib.sha256(destination.read_bytes()).digest() != hashlib.sha256(package.read_bytes()).digest():
                raise ValueError(f'Different packages share {name} {version}; publish a new version')
        else:
            shutil.copyfile(package, destination)
        names.add(name)
    if not {'reread', 'reread-app-camera'} <= names:
        raise ValueError('The stable repository needs both ReRead and Camera')
    indexes = archive / 'dists/stable/main/binary-i386'
    indexes.mkdir(parents=True)
    packages_file = indexes / 'Packages'
    with packages_file.open('wb') as target:
        run('apt-ftparchive', 'packages', 'pool/main', cwd=archive, stdout=target)
    (indexes / 'Packages.gz').write_bytes(gzip.compress(packages_file.read_bytes(), mtime=0))
    distribution = archive / 'dists/stable'
    options = dict(Origin='ReRead', Label='ReRead', Suite='stable', Codename='stable',
                   Architectures='i386', Components='main', Description='ReRead platform and applications')
    args = ['apt-ftparchive']
    for name, value in options.items():
        args += ['-o', f'APT::FTPArchive::Release::{name}={value}']
    with (distribution / 'Release').open('wb') as target:
        run(*args, 'release', 'dists/stable', cwd=archive, stdout=target)
    for arguments, filename in [(['--clearsign'], 'InRelease'),
                                (['--armor', '--detach-sign'], 'Release.gpg')]:
        run('gpg', '--batch', '--yes', '--digest-algo', 'SHA256', '--local-user', key,
            '--output', str(distribution / filename), *arguments, str(distribution / 'Release'))
    public_key = BASE / 'reread-archive-keyring.asc'
    # Verify against the public key that Readers will actually trust, independently
    # of the signing machine's keyring. A mismatched signing secret must fail here.
    with tempfile.TemporaryDirectory() as temporary:
        keyring = Path(temporary) / 'archive.gpg'
        run('gpg', '--batch', '--yes', '--dearmor', '--output', str(keyring), str(public_key))
        run('gpgv', '--keyring', str(keyring), str(distribution / 'InRelease'))
    shutil.copyfile(public_key, archive / public_key.name)
    source = (BASE / 'reread.sources').read_text().replace(URL, url.rstrip('/'))
    (archive / 'reread.sources').write_text(source)
    links = '\n'.join(f'<li><a href="apt/pool/main/{html.escape(p.name, quote=True)}">{html.escape(p.name)}</a></li>'
                      for p in sorted(pool.glob('*.deb')))
    (output / 'index.html').write_text('''<!doctype html><html lang="en"><meta charset="utf-8">
<title>ReRead APT repository</title><h1>ReRead APT repository</h1>
<p>Stable i386 packages for the Intel Reader. APT installs and updates the platform
and apps; antiX supplies their system dependencies.</p>
<p><a href="apt/reread.sources">APT source</a> ·
<a href="apt/reread-archive-keyring.asc">Archive public key</a> ·
<a href="https://github.com/legop3/ReRead/releases">Releases and installers</a></p>
<ul>''' + links + '</ul></html>\n')
    (output / '.nojekyll').touch()
    print(f'Built signed archive with {len(list(pool.glob("*.deb")))} packages in {output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('packages', type=Path, help='Directory containing published .deb files')
    parser.add_argument('--output', type=Path, required=True, help='New output directory')
    parser.add_argument('--key', required=True, help='GnuPG signing key fingerprint')
    parser.add_argument('--url', default=URL)
    args = parser.parse_args()
    build(list(args.packages.rglob('*.deb')), args.output, args.key, args.url)
