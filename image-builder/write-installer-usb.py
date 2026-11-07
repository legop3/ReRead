"""Write the built installer only to the USB stick authorized in this session."""
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess

BASE = Path(__file__).resolve().parents[1]
USB = Path('/dev/disk/by-id/usb-_USB_Flash_Memory_00D0C9CCDF48ED8160007561-0:0')
image = BASE / 'out/reader-installer-usb.img'
manifest = json.loads((BASE / 'out/manifest.json').read_text())
expected = manifest['installer_sha256']

if os.geteuid() != 0:
    raise SystemExit('Run with sudo. This writes only the specifically authorized USB stick.')
with image.open('rb') as stream:
    assert hashlib.file_digest(stream, 'sha256').hexdigest() == expected, 'Installer checksum mismatch'
device = USB.resolve(strict=True)
assert stat.S_ISBLK(device.stat().st_mode)
tree = json.loads(subprocess.check_output([
    'lsblk', '-J', '-b', '-o', 'PATH,SIZE,TRAN,RM,MOUNTPOINTS', str(device)
]))['blockdevices'][0]
assert tree['tran'] == 'usb' and tree['rm'], 'Not removable USB'
assert int(tree['size']) == 15610576896, 'USB identity or size changed'

def unmounted(node):
    assert not any(node.get('mountpoints') or []), 'Unmount USB filesystems first'
    for child in node.get('children', []):
        unmounted(child)

unmounted(tree)
print(f'Writing installer to {USB} ({device})', flush=True)
fd = os.open(device, os.O_RDWR | os.O_EXCL)
try:
    assert os.fstat(fd).st_rdev == device.stat().st_rdev
    with image.open('rb') as source:
        total = 0
        while chunk := source.read(4 * 1024 * 1024):
            view = memoryview(chunk)
            while view:
                count = os.write(fd, view)
                assert count > 0
                view = view[count:]
            total += len(chunk)
            if total % (256 * 1024 * 1024) == 0:
                print(f'Written {total // (1024 * 1024)} MiB', flush=True)
    os.fsync(fd)
    os.lseek(fd, 0, os.SEEK_SET)
    check = hashlib.sha256()
    remaining = image.stat().st_size
    print('Verifying USB readback...', flush=True)
    while remaining:
        chunk = os.read(fd, min(4 * 1024 * 1024, remaining))
        assert chunk, 'Unexpected end of device'
        check.update(chunk)
        remaining -= len(chunk)
    assert check.hexdigest() == expected, 'USB readback mismatch'
finally:
    os.close(fd)
print('USB VERIFIED. Boot it on the Reader; Enter selects the stock-kernel installer.', flush=True)
