"""Write the built installer only to an authorized USB stick."""
import json
import os
from pathlib import Path
import stat
import subprocess

BASE = Path(__file__).resolve().parents[1]
USB_DRIVES = {
    '/dev/disk/by-id/usb-_USB_Flash_Memory_00D0C9CCDF48ED8160007561-0:0': 15610576896,
    '/dev/disk/by-id/usb-_USB_DISK_3.0_0719079F1B2AD607-0:0': 15854469120,
}
output = BASE / 'out/antix'
manifest = json.loads((output / 'manifest.json').read_text())
image = output / manifest['installer_usb_image']

if os.geteuid() != 0:
    raise SystemExit('Run with sudo. This writes only the specifically authorized USB stick.')
connected = [Path(path) for path in USB_DRIVES if Path(path).exists()]
if len(connected) != 1:
    raise SystemExit('Connect exactly one authorized USB stick; unplug the other if both are connected.')
USB = connected[0]
device = USB.resolve(strict=True)
assert stat.S_ISBLK(device.stat().st_mode)
tree = json.loads(subprocess.check_output([
    'lsblk', '-J', '-b', '-o', 'PATH,SIZE,TRAN,RM,MOUNTPOINTS', str(device)
]))['blockdevices'][0]
assert tree['tran'] == 'usb' and tree['rm'], 'Not removable USB'
assert int(tree['size']) == USB_DRIVES[str(USB)], 'USB identity or size changed'

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
finally:
    os.close(fd)
print('USB READY. Boot it on the Reader: automatic install, then reboot.', flush=True)
print('Keep USB connected until INSTALLATION COMPLETE; it may be removed during the reboot countdown.', flush=True)
