"""Pinned upstream file manager, bundled for offline installs and USB-only use."""
import hashlib
from pathlib import Path
import shutil
import tarfile
from urllib.request import urlopen

COPYPARTY_VERSION = '1.20.26'
COPYPARTY_SHA256 = 'ac7ea05bde8d431b32cc2ff618f2754e46bd967bf10deaa6654dac9a449a6ff9'


def stage_file_manager(root, base):
    name = f'copyparty-{COPYPARTY_VERSION}'
    archive = base / 'downloads/copyparty' / (name + '.tar.gz')
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        url = f'https://github.com/9001/copyparty/releases/download/v{COPYPARTY_VERSION}/{name}.tar.gz'
        temporary = archive.with_suffix('.part')
        with urlopen(url, timeout=60) as source, temporary.open('wb') as dest:
            shutil.copyfileobj(source, dest)
        temporary.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != COPYPARTY_SHA256:
        raise RuntimeError(f'Copyparty archive checksum mismatch: {archive}')
    with tarfile.open(archive) as source:
        for entry in source:
            if not entry.isfile():
                continue
            path = Path(entry.name)
            if path.parts[:2] == (name, 'copyparty') and '..' not in path.parts:
                if '__pycache__' in path.parts or path.suffix == '.pyc':
                    continue
                target = root / 'usr/lib/reread/vendor' / Path(*path.parts[1:])
            elif entry.name == name + '/LICENSE.txt':
                target = root / 'usr/share/doc/reread/copyparty-LICENSE.txt'
            else:
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with source.extractfile(entry) as data, target.open('wb') as dest:
                shutil.copyfileobj(data, dest)
            target.chmod(0o644)
