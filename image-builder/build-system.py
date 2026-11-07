"""Run inside the builder container; writes regular image files only."""
import os
from pathlib import Path
import shutil
import subprocess

BASE = Path('/work')
ROOT = BASE / 'build/debian-root'
OUT = BASE / 'out'
UUID = '3b1293e9-5462-4b91-b10e-d9fd5e0f2616'
MIB = 1024 * 1024

def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)

kernel = sorted((ROOT / 'boot').glob('vmlinuz-*'))[-1]
version = kernel.name.removeprefix('vmlinuz-')
initrd = ROOT / ('boot/initrd.img-' + version)
(ROOT / 'boot/grub').mkdir(exist_ok=True)
config = 'set timeout=4\nset default=0\nserial --unit=0 --speed=115200\nterminal_input console serial\nterminal_output console serial\n'
for title, extra in [('Reader Workbench desktop', ''), ('Text console', 'reader.text=1'), ('Desktop with basic graphics', 'nomodeset')]:
    config += f'''menuentry '{title}' {{
 search --no-floppy --fs-uuid --set=root {UUID}
 linux /boot/{kernel.name} root=UUID={UUID} ro net.ifnames=0 {extra}
 initrd /boot/{initrd.name}
}}
'''
(ROOT / 'boot/grub/grub.cfg').write_text(config)
finish = ROOT / 'usr/local/sbin/reader-finish-image'
finish.write_text('''#!/bin/sh
set -eu
[ -r /proc/self/mounts ] || mount -t proc proc /proc
[ -d /sys/block ] || mount -t sysfs sysfs /sys
mkdir -p /run
grub-install --target=i386-pc --boot-directory=/boot --recheck /dev/sda
rm /usr/local/sbin/reader-finish-image
sync
mount -o remount,ro / || true
echo READER_GRUB_INSTALL_OK
/bin/busybox poweroff -f
''')
finish.chmod(0o755)
part = BASE / 'build/root-partition.img'
with part.open('wb') as f:
    f.truncate(3583 * MIB)
run('mke2fs', '-q', '-F', '-t', 'ext4', '-b', '4096', '-m', '1', '-U', UUID,
    '-L', 'READERROOT', '-d', str(ROOT), str(part))
image = OUT / 'reader-workbench.img'
with image.open('wb') as f:
    f.truncate(3584 * MIB)
run('sfdisk', str(image), input=b'label: dos\nunit: sectors\n\nstart=2048,type=83,bootable\n')
with image.open('r+b') as dest, part.open('rb') as source:
    # Preserve holes rather than physically copying several GiB of zero blocks.
    position = 0
    while position < part.stat().st_size:
        try:
            data = os.lseek(source.fileno(), position, os.SEEK_DATA)
        except OSError as error:
            if error.errno == 6:  # ENXIO: no more allocated extents.
                break
            raise
        end = os.lseek(source.fileno(), data, os.SEEK_HOLE)
        source.seek(data)
        dest.seek(MIB + data)
        remaining = end - data
        while remaining:
            chunk = source.read(min(8 * MIB, remaining))
            assert chunk
            dest.write(chunk)
            remaining -= len(chunk)
        position = end
log = OUT / 'build-grub-vm.log'
with log.open('wb') as output:
    run('timeout', '240', 'qemu-system-i386', '-machine', 'accel=kvm:tcg', '-m', '512', '-cpu', 'qemu32',
        '-drive', f'file={image},format=raw,if=ide', '-kernel', str(kernel),
        '-initrd', str(initrd), '-append',
        f'root=UUID={UUID} rw init=/usr/local/sbin/reader-finish-image console=ttyS0,115200',
        '-display', 'none', '-serial', 'stdio', '-monitor', 'none', '-no-reboot',
        stdout=output, stderr=subprocess.STDOUT)
assert 'READER_GRUB_INSTALL_OK' in log.read_text(), 'GRUB installation did not complete'
print(f'Bootable system image built: {image}', flush=True)
