"""Write the verified antiX Core 32-bit test ISO only to the specifically authorized USB stick."""
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess

BASE = Path(__file__).resolve().parents[1]
ISO = BASE / 'downloads/antiX-26.1_386-core.iso'
EXPECTED = '5e07ed85acba1d2c33a7e41b089e80ce203eeb11d81af8c830e64e2e058ead7e'
USB = Path('/dev/disk/by-id/usb-_USB_Flash_Memory_00D0C9CCDF48ED8160007561-0:0')

def digest_file(path):
    with open(path, 'rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

assert digest_file(ISO) == EXPECTED, 'ISO checksum mismatch'
device = USB.resolve(strict=True)
assert stat.S_ISBLK(device.stat().st_mode), 'Target is not a block device'
tree = json.loads(subprocess.check_output([
    'lsblk', '-J', '-b', '-o', 'PATH,SIZE,TRAN,RM,MOUNTPOINTS', str(device)
]))['blockdevices'][0]
assert tree['tran'] == 'usb' and tree['rm'], 'Not a removable USB device'
assert int(tree['size']) == 15610576896, 'USB size changed'

def check_unmounted(node):
    assert not any(node.get('mountpoints') or []), 'Target has mounted filesystems'
    for child in node.get('children', []):
        check_unmounted(child)

check_unmounted(tree)
print(f'Verified ISO; writing {ISO.stat().st_size} bytes to {USB} ({device})', flush=True)
if os.geteuid() == 0:
    fd = os.open(device, os.O_RDWR | os.O_EXCL)
else:
    from gi.repository import Gio, GLib
    bus = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
    reply, descriptors = bus.call_with_unix_fd_list_sync(
        'org.freedesktop.UDisks2',
        '/org/freedesktop/UDisks2/block_devices/' + device.name,
        'org.freedesktop.UDisks2.Block', 'OpenForRestore',
        GLib.Variant('(a{sv})', ({'auth.no_user_interaction': GLib.Variant('b', True)},)),
        GLib.VariantType.new('(h)'), Gio.DBusCallFlags.NONE, 30000, None, None,
    )
    fd = descriptors.get(reply.unpack()[0])
try:
    assert os.fstat(fd).st_rdev == device.stat().st_rdev, 'Opened device mismatch'
    with ISO.open('rb') as source:
        written = 0
        while chunk := source.read(4 * 1024 * 1024):
            remaining = memoryview(chunk)
            while remaining:
                count = os.write(fd, remaining)
                assert count > 0
                remaining = remaining[count:]
            written += len(chunk)
            if written % (256 * 1024 * 1024) == 0:
                print(f'Written {written // (1024 * 1024)} MiB', flush=True)
    os.fsync(fd)
    os.lseek(fd, 0, os.SEEK_SET)
    digest = hashlib.sha256()
    remaining = ISO.stat().st_size
    while remaining:
        chunk = os.read(fd, min(4 * 1024 * 1024, remaining))
        assert chunk, 'Unexpected end of device'
        digest.update(chunk)
        remaining -= len(chunk)
    assert digest.hexdigest() == EXPECTED, 'USB readback checksum mismatch'
    print('USB write and complete ISO-length SHA-256 readback verified.', flush=True)
finally:
    os.close(fd)
