"""Optional extraction of stock firmware for hardware research; not a ReRead build input."""
from pathlib import Path
import shutil
import subprocess

BASE = Path('/work')
source = BASE / 'evidence/reader.img'
assert source.stat().st_size == 4004388864
parts = [('boot', 63, 48132), ('root', 48195, 3325455)]
(BASE / 'analysis/partitions').mkdir(parents=True, exist_ok=True)
with source.open('rb') as disk:
    for name, start, count in parts:
        dest = BASE / 'analysis/partitions' / (name + '.img')
        if dest.exists():
            assert dest.stat().st_size == count * 512
            continue
        disk.seek(start * 512)
        remaining = count * 512
        with dest.open('wb') as output:
            while remaining:
                chunk = disk.read(min(8 * 1024 * 1024, remaining))
                assert chunk
                output.write(chunk)
                remaining -= len(chunk)

def extract(part, inside, destination):
    if destination.exists():
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['debugfs', '-R', f'dump -p {inside} {destination}',
                    str(BASE / 'analysis/partitions' / (part + '.img'))], check=True)
    assert destination.exists()

boot = BASE / 'analysis/stock-boot'
extract('boot', '/vmlinuz-2.6.27-4-lpia', boot / 'vmlinuz-2.6.27-4-lpia')
extract('boot', '/initrd.img-2.6.27-4-lpia', boot / 'initrd.img-2.6.27-4-lpia')
for path in ['bin/dd', 'bin/gzip', 'usr/bin/sha256sum']:
    extract('root', '/' + path, BASE / 'analysis/stock-tools' / path)
initrd = BASE / 'build/stock-initrd'
if not (initrd / 'init').exists():
    initrd.mkdir(parents=True, exist_ok=True)
    subprocess.run(['sh', '-c', f'gzip -dc {boot}/initrd.img-2.6.27-4-lpia | cpio -id --quiet'], cwd=initrd, check=True)
modules = BASE / 'analysis/stock-root/lib/modules'
if not modules.exists():
    modules.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['debugfs', '-R', f'rdump /lib/modules {modules.parent}',
                    str(BASE / 'analysis/partitions/root.img')], check=True)
assert modules.exists()
