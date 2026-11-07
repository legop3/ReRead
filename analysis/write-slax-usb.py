"""Prepare the authorized USB as an MBR/FAT32 Slax live boot disk."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile

BASE = Path(__file__).resolve().parents[1]
ISO = BASE / 'downloads/slax-32bit-debian-12.2.0.iso'
SHA256 = '03b85cd259883f6781b3a3f30ed409b0b6a542b8f510094594c7600bd94e546b'
USB = Path('/dev/disk/by-id/usb-_USB_Flash_Memory_00D0C9CCDF48ED8160007561-0:0')

def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

if os.geteuid() != 0:
    raise SystemExit('Run this script with sudo.')
assert digest(ISO) == SHA256, 'ISO checksum mismatch'
device = USB.resolve(strict=True)
assert stat.S_ISBLK(device.stat().st_mode), 'Not a block device'
tree = json.loads(subprocess.check_output([
    'lsblk', '-J', '-b', '-o', 'PATH,SIZE,TRAN,RM,MOUNTPOINTS', str(device)
]))['blockdevices'][0]
assert tree['tran'] == 'usb' and tree['rm'], 'Not removable USB'
assert int(tree['size']) == 15610576896, 'USB identity/size changed'

def check_unmounted(node):
    assert not any(node.get('mountpoints') or []), 'USB is mounted; unmount it first'
    for child in node.get('children', []):
        check_unmounted(child)

check_unmounted(tree)
for command in ['7z', 'sfdisk', 'wipefs', 'mkfs.vfat', 'udevadm', 'mount', 'umount']:
    assert shutil.which(command), f'Missing command: {command}'

with tempfile.TemporaryDirectory(prefix='intelreader-slax-') as tmp:
    tmp = Path(tmp)
    files = tmp / 'files'
    run('7z', 'x', '-y', '-o' + str(files), str(ISO), 'slax/*', stdout=subprocess.DEVNULL)
    boot = files / 'slax/boot'
    installer = boot / 'extlinux.x64'
    installer.chmod(0o755)
    # Check that the host can execute the bundled installer before erasing USB.
    run(str(installer), '--version')
    config = boot / 'syslinux.cfg'
    config.write_text(config.read_text().replace(' automount', ''))
    originals = {p.relative_to(files): digest(p) for p in files.rglob('*') if p.is_file()}
    mountpoint = tmp / 'usb'
    mountpoint.mkdir()
    print(f'Erasing authorized USB: {USB} ({device}), creating MBR/FAT32.', flush=True)
    run('wipefs', '--all', '--force', str(device))
    run('sfdisk', '--wipe', 'always', str(device), input=b'label: dos\nunit: sectors\n\nstart=2048, type=c, bootable\n')
    run('udevadm', 'settle')
    part = Path(str(device) + '1')
    assert part.is_block_device(), 'Partition device did not appear'
    run('mkfs.vfat', '-F', '32', '-n', 'SLAXTEST', str(part))
    run('mount', '-t', 'vfat', '-o', 'rw,nosuid,nodev', str(part), str(mountpoint))
    try:
        shutil.copytree(files / 'slax', mountpoint / 'slax')
        # Same loader and 440-byte MBR as bootinst.sh; sfdisk already set boot flag.
        run(str(installer), '--install', str(mountpoint / 'slax/boot'))
        with device.open('r+b', buffering=0) as disk:
            code = (boot / 'mbr.bin').read_bytes()
            assert len(code) == 440
            assert disk.write(code) == len(code)
            os.fsync(disk.fileno())
        # Move optional UEFI files as the vendor bootinst.sh does.
        shutil.move(str(mountpoint / 'slax/boot/EFI'), str(mountpoint / 'EFI'))
        os.sync()
    finally:
        run('umount', str(mountpoint))
    run('mount', '-t', 'vfat', '-o', 'ro,nosuid,nodev,noexec', str(part), str(mountpoint))
    try:
        for relative, expected in originals.items():
            relative_text = str(relative)
            if relative_text.startswith('slax/boot/EFI/'):
                relative_text = relative_text.replace('slax/boot/EFI/', 'EFI/', 1)
            assert digest(mountpoint / relative_text) == expected, f'Copy verification failed: {relative}'
        assert (mountpoint / 'slax/boot/ldlinux.sys').is_file(), 'Bootloader missing'
    finally:
        run('umount', str(mountpoint))
print('Slax USB prepared and copied files verified. Safely remove it and boot the Reader.', flush=True)
