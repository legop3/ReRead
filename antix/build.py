"""Build SSD-installed antiX Core and its automatic USB installer.
Run as root in intelreader-validator; inputs/outputs are regular workspace files.
"""
import gzip
import os
from pathlib import Path
import shutil
import stat
import uuid

BASE=Path('/work')
BUILD=BASE/'build/antix'
OUT=BASE/'out/antix'
ISO=BASE/'downloads/antiX-26.1_386-core.iso'
EXPECTED='5e07ed85acba1d2c33a7e41b089e80ce203eeb11d81af8c830e64e2e058ead7e'
KERNEL='5.10.256-antix.1-486-smp'
MIB=1024*1024
import image
from gadget import configure as configure_gadget
from package_installer import package
from release import build as build_release
from trim import runtime as trim_runtime, policy as storage_policy
run=image.run
OUT.mkdir(parents=True,exist_ok=True)
BUILD.mkdir(parents=True,exist_ok=True)
base=BUILD/'base-root'
if not base.exists():
    import hashlib
    with ISO.open("rb") as source:
        assert hashlib.file_digest(source,"sha256").hexdigest()==EXPECTED, "ISO checksum mismatch"
    run('7z','x','-y','-o'+str(BUILD/'iso'),str(ISO),'antiX/linuxfs','antiX/vmlinuz')
    run('unsquashfs','-no-progress','-d',str(base),str(BUILD/'iso/antiX/linuxfs'))
root=BUILD/'root'
if root.exists():shutil.rmtree(root)
run('cp','-a',str(base),str(root))
root.chmod(0o755)

def write(path,content,executable=False):
    p=root/path.lstrip('/')
    p.parent.mkdir(parents=True,exist_ok=True)
    p.write_text(content)
    if executable:p.chmod(0o755)

def copy(source,path):
    p=root/path.lstrip('/');p.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy(source,p)

# The ISO's live /dev can contain placeholder regular files. Build real devices.
for name,major,minor,mode in [('null',1,3,0o666),('zero',1,5,0o666),('random',1,8,0o666),('urandom',1,9,0o666),('console',5,1,0o600),('tty',5,0,0o666)]:
    p=root/'dev'/name
    if p.exists() or p.is_symlink():p.unlink()
    os.mknod(p,stat.S_IFCHR|mode,os.makedev(major,minor))
root_id=str(uuid.uuid4())
boot_serial=uuid.uuid4().hex[:8]
boot_id=boot_serial[:4].upper()+'-'+boot_serial[4:].upper()
write('/etc/hostname','reread\n')
write('/etc/reader-root-id',root_id+'\n')
write('/etc/fstab',f'UUID={root_id} / ext3 defaults,noatime 0 1\nUUID={boot_id} /boot vfat defaults,noatime,umask=0077 0 0\n')
# Do not embed the live ISO's shared demo/root passwords in the installed image.
shadow=root/'etc/shadow'
shadow.write_text('\n'.join(':'.join([parts[0],'!',*parts[2:]]) if parts[0] in ('root','demo') else line
    for line in shadow.read_text().splitlines() for parts in [line.split(':')])+'\n')
# The image and later updates install the same versioned ReRead package.
storage_policy(root)
configure_gadget(root, BASE, BUILD, KERNEL)
release = build_release(BASE, BUILD, OUT, KERNEL)
local_package = root/'tmp'/release.name
shutil.copy(release, local_package)
run('chroot',str(root),'dpkg','--force-confdef','--force-confold','-i','/tmp/'+release.name)
local_package.unlink()
for name in ('camera', 'software', 'files'):
    app_version = (BASE / f'antix/apps/{name}/VERSION').read_text().strip()
    app_package = OUT / f'reread-app-{name}_{app_version}_i386.deb'
    shutil.copy(app_package, root / 'tmp' / app_package.name)
    run('chroot', str(root), 'dpkg', '-i', '/tmp/' + app_package.name)
    (root / 'tmp' / app_package.name).unlink()
# The cached base retains headers for later release builds; the runtime does not.
trim_runtime(root)
write('/etc/default/console-setup','ACTIVE_CONSOLES="/dev/tty[1-6]"\nCHARMAP="UTF-8"\nFONT="/usr/share/reader/font-large.psf"\n')
# Include the tested Reader storage path and the same early large console font.
write('/etc/initramfs-tools/modules','pata_sch\nata_piix\nsd_mod\next3\n')
write('/etc/initramfs-tools/conf.d/reader','MODULES=most\nCOMPRESS=gzip\n')
write('/etc/initramfs-tools/hooks/reader-font', '''#!/bin/sh
PREREQ=""
case "$1" in prereqs) echo "$PREREQ"; exit 0 ;; esac
. /usr/share/initramfs-tools/hook-functions
copy_exec /usr/bin/setfont /bin/setfont
mkdir -p "$DESTDIR/usr/share/reader"
cp /usr/share/reader/font-*.psf "$DESTDIR/usr/share/reader/"
''',True)
write('/etc/initramfs-tools/scripts/init-top/reader-font', '''#!/bin/sh
PREREQ="udev"
case "$1" in prereqs) echo "$PREREQ"; exit 0 ;; esac
[ -c /dev/tty0 ] || mknod /dev/tty0 c 4 0
/bin/setfont -C /dev/tty0 /usr/share/reader/font-large.psf ||
    /bin/setfont -C /dev/tty0 /usr/share/reader/font-tall.psf || true
''',True)
# An installed initramfs, not antiX's live-media discovery initramfs.
run('chroot',str(root),'update-initramfs','-u','-k',KERNEL)
files=BUILD/'system-files'
if files.exists():shutil.rmtree(files)
# FAT is the actual /boot filesystem, so distro kernel/initramfs tools update it.
# FAT has no symlinks; the bootloader names the tested kernel explicitly.
files.mkdir(parents=True)
for source in (root/'boot').iterdir():
    if source.is_file() and not source.is_symlink():
        shutil.move(source,files/source.name)
    elif source.is_dir():
        shutil.rmtree(source)
    else:
        source.unlink()
(files/'syslinux.cfg').write_text(f'''DEFAULT normal
PROMPT 1
TIMEOUT 30
TOTALTIMEOUT 30
ONTIMEOUT normal
SERIAL 0 115200
LABEL normal
 KERNEL /vmlinuz-{KERNEL}
 INITRD /initrd.img-{KERNEL}
 APPEND root=UUID={root_id} rootfstype=ext3 ro console=tty0 loglevel=3 i8042.dumbkbd=1
LABEL serial
 KERNEL /vmlinuz-{KERNEL}
 INITRD /initrd.img-{KERNEL}
 APPEND root=UUID={root_id} rootfstype=ext3 ro console=ttyS0,115200 loglevel=3 i8042.dumbkbd=1
''')
shutil.copy('/usr/lib/syslinux/modules/bios/ldlinux.c32',files/'ldlinux.c32')
rootfs=BUILD/'root-ext3.img'
with rootfs.open('wb') as f:f.truncate(image.ROOT_BYTES)
run('mke2fs','-q','-F','-t','ext3','-m','1','-L','REREADROOT','-U',root_id,'-d',str(root),str(rootfs))
system=image.disk('reread',image.SSD_SECTORS*512//MIB,files,rootfs,boot_serial=boot_serial)
packed=OUT/'reread.img.gz'
with system.open('rb') as source,packed.open('wb') as dest:
    with gzip.GzipFile(filename='',mode='wb',fileobj=dest,mtime=0,compresslevel=1) as zipped:
        shutil.copyfileobj(source,zipped,4*MIB)
package(system,packed,root_id)
