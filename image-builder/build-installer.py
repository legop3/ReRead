"""Create a BIOS-bootable USB image with stock and modern RAM installers."""
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

BASE = Path('/work')
ROOT = BASE / 'build/debian-root'
OUT = BASE / 'out'
MIB = 1024 * 1024

def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

raw = OUT / 'reader-workbench.img'
compressed = OUT / 'reader-workbench.img.gz'
raw_digest = hashlib.sha256()
with raw.open('rb') as source, compressed.open('wb') as dest:
    with gzip.GzipFile(filename='', mode='wb', compresslevel=1, fileobj=dest, mtime=0) as packed:
        while chunk := source.read(8 * MIB):
            raw_digest.update(chunk)
            packed.write(chunk)
raw_sha = raw_digest.hexdigest()
packed_sha = sha(compressed)
info = f"IMAGE_SHA256={raw_sha}\nIMAGE_GZ_SHA256={packed_sha}\nIMAGE_MIB={raw.stat().st_size // MIB}\n"
kernel = sorted((ROOT / 'boot').glob('vmlinuz-*'))[-1]
version = kernel.name.removeprefix('vmlinuz-')
files = BASE / 'build/installer-files'
if files.exists():
    shutil.rmtree(files)
(files / 'boot').mkdir(parents=True)
(files / 'payload').mkdir()
(files / 'READER-INSTALLER').write_text('Reader Workbench installer v0.1\n')

for flavor in ['stock', 'modern']:
    tree = BASE / ('build/installer-' + flavor)
    if tree.exists():
        shutil.rmtree(tree)
    shutil.copytree(BASE / 'build/stock-initrd', tree, symlinks=True)
    for source, dest in [('installer-init.sh', 'init'), ('installer-menu.sh', 'installer-menu')]:
        shutil.copyfile(BASE / 'image-builder' / source, tree / dest)
        (tree / dest).chmod(0o755)
    (tree / 'image-info').write_text(info)
    # GNU versions with known SHA-256, gzip integrity, and full-block dd support.
    shutil.copyfile(BASE / 'analysis/stock-tools/bin/dd', tree / 'bin/dd')
    shutil.copyfile(BASE / 'analysis/stock-tools/bin/gzip', tree / 'bin/gzip')
    (tree / 'usr/bin').mkdir(parents=True, exist_ok=True)
    shutil.copyfile(BASE / 'analysis/stock-tools/usr/bin/sha256sum', tree / 'usr/bin/sha256sum')
    for executable in ['bin/dd', 'bin/gzip', 'usr/bin/sha256sum']:
        (tree / executable).chmod(0o755)
    # Original initramfs already supplies the matching old libc and loader.
    for applet in ['readlink', 'stat', 'head', 'tr', 'printf', 'grep', 'sed', 'awk', 'sort']:
        dest = tree / 'bin' / applet
        if not dest.exists():
            dest.symlink_to('busybox')
    if flavor == 'stock':
        shutil.copytree(BASE / 'analysis/stock-root/lib/modules', tree / 'lib/modules', dirs_exist_ok=True)
        shutil.copyfile(BASE / 'analysis/stock-boot/vmlinuz-2.6.27-4-lpia', files / 'boot/stock-kernel')
    else:
        shutil.rmtree(tree / 'lib/modules')
        (tree / 'lib/modules' / version).mkdir(parents=True)
        modules = ['ehci_pci', 'uhci_hcd', 'ohci_pci', 'xhci_pci', 'usb_storage', 'uas',
                   'usbhid', 'hid_generic', 'hid_apple', 'sd_mod', 'ahci', 'ata_piix',
                   'ata_generic', 'pata_sch', 'virtio_pci', 'virtio_blk', 'ext2', 'vfat',
                   'nls_cp437', 'nls_iso8859_1']
        for module in modules:
            output = subprocess.check_output(['modprobe', '--show-depends', '-d', str(ROOT), '-S', version, module], text=True)
            for line in output.splitlines():
                if line.startswith('insmod '):
                    source = Path(line.split()[1])
                    relative = source.relative_to(ROOT)
                    dest = tree / relative
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(source, dest)
        for name in ['modules.builtin', 'modules.builtin.modinfo', 'modules.order']:
            source = ROOT / 'lib/modules' / version / name
            if source.exists():
                shutil.copyfile(source, tree / 'lib/modules' / version / name)
        run('depmod', '-b', str(tree), version)
        shutil.copyfile(kernel, files / 'boot/modern-kernel')
    for name, major, minor in [('console', 5, 1), ('null', 1, 3)]:
        node = tree / 'dev' / name
        if not node.exists():
            os.mknod(node, 0o20600, os.makedev(major, minor))
    output = files / 'boot' / (flavor + '-initrd.gz')
    # cpio records device nodes and symlinks; kernel needs /dev/console at exec.
    with output.open('wb') as dest:
        cpio = subprocess.Popen(['sh', '-c', 'find . -print0 | sort -z | cpio --null -o -H newc --owner=0:0 --quiet'], cwd=tree, stdout=subprocess.PIPE)
        with gzip.GzipFile(filename='', mode='wb', compresslevel=6, fileobj=dest, mtime=0) as packed:
            shutil.copyfileobj(cpio.stdout, packed, 8 * MIB)
        assert cpio.wait() == 0

shutil.copyfile(compressed, files / 'payload/reader-workbench.img.gz')
config = '''DEFAULT stock
PROMPT 1
TIMEOUT 100
SERIAL 0 115200
DISPLAY boot.msg
'''
for name in ['stock', 'modern']:
    for serial in [False, True]:
        label = name + ('-serial' if serial else '')
        console = 'ttyS0,115200' if serial else 'tty0'
        config += f'''LABEL {label}
 KERNEL /boot/{name}-kernel
 INITRD /boot/{name}-initrd.gz
 APPEND console={console} loglevel=3
'''
(files / 'syslinux.cfg').write_text(config)
(files / 'boot.msg').write_text('''Reader Workbench installer — no automatic disk writes

Press Enter for the stock-kernel installer (recommended for the Reader).
Or type modern and press Enter for the newer kernel.
The installer will ask before erasing the internal SSD.
'''.replace('—', '-'))
part = BASE / 'build/installer-fat.img'
with part.open('wb') as dest:
    dest.truncate(2047 * MIB)
run('mkfs.vfat', '-F', '32', '-n', 'READERINST', str(part))
run('mcopy', '-s', '-i', str(part), *map(str, files.iterdir()), '::/')
run('syslinux', '--install', str(part))
usb = OUT / 'reader-installer-usb.img'
with usb.open('wb') as dest:
    dest.truncate(2048 * MIB)
run('sfdisk', str(usb), input=b'label: dos\nunit: sectors\n\nstart=2048,type=c,bootable\n')
with usb.open('r+b') as dest, part.open('rb') as source:
    dest.write(Path('/usr/lib/syslinux/mbr/mbr.bin').read_bytes())
    dest.seek(MIB)
    shutil.copyfileobj(source, dest, 8 * MIB)
manifest = {'system_image': raw.name, 'system_bytes': raw.stat().st_size,
            'system_sha256': raw_sha, 'compressed_image': compressed.name,
            'compressed_sha256': packed_sha, 'installer_usb_image': usb.name,
            'installer_bytes': usb.stat().st_size, 'installer_sha256': sha(usb),
            'kernel': version, 'hardware_status': 'untested on Reader'}
(OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
(OUT / 'SHA256SUMS').write_text(f'{raw_sha}  {raw.name}\n{packed_sha}  {compressed.name}\n{manifest["installer_sha256"]}  {usb.name}\n')
print(json.dumps(manifest, indent=2), flush=True)
