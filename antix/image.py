"""Disk-image helpers and the small USB installer environment."""
import gzip
import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid

BASE=Path('/work')
BUILD=BASE/'build/antix'
OUT=BASE/'out/antix'
MIB=1024*1024
BUILD.mkdir(exist_ok=True)
OUT.mkdir(exist_ok=True)

def run(*args,**kwargs): return subprocess.run(args,check=True,**kwargs)
def installer_tree():
    role="installer"
    root=BUILD/role
    if root.exists(): shutil.rmtree(root)
    for d in ['bin','sbin','usr/bin','lib','dev','proc','sys','tmp','root','etc','var/lib/reader','var/log/reader']:
        (root/d).mkdir(parents=True,exist_ok=True)
    old=BASE/'build/stock-initrd'
    shutil.copy(old/'bin/busybox',root/'bin/busybox')
    for name in ['sh','mount','umount','mknod','mkdir','cat','sleep','readlink','grep','printf','tr','sync','uname','ls','dmesg','rm','echo','ln']:
        # dmesg is supplied by klibc below, not by this BusyBox build.
        if name!='dmesg': (root/'bin'/name).symlink_to('busybox')
    for name in ['libc.so.6','ld-linux.so.2','librt.so.1','libpthread.so.0','libdl.so.2']:
        shutil.copy(old/'lib'/name,root/'lib'/name)
    for p in (old/'lib').glob('klibc-*.so'): shutil.copy(p,root/'lib'/p.name)
    for name in ['dmesg','setfont']:
        shutil.copy(old/'bin'/name,root/'bin'/name)
    shutil.copy(old/'sbin/modprobe',root/'sbin/modprobe')
    shutil.copytree(BASE/'analysis/stock-root/lib/modules',root/'lib/modules',symlinks=True,copy_function=shutil.copy)
    # Keep only the modules used by this boot/input proof and their dependencies.
    module_root=root/'lib/modules/2.6.27-4-lpia'
    dependencies={}
    for line in (module_root/'modules.dep').read_text().splitlines():
        path, rest=line.split(':',1)
        dependencies[path]=rest.split()
    wanted=set()
    def retain(path):
        if path in wanted:return
        wanted.add(path)
        for dependency in dependencies.get(path,[]):retain(dependency)
    names={'ehci_hcd','uhci_hcd','usbhid','hid','evdev','sd_mod','fbcon','vesafb','button'}
    if role == 'installer':
        names.update({'usb_storage','vfat','nls_cp437','nls_iso8859_1'})
    for path in dependencies:
        if Path(path).stem.replace('-','_') in names:retain(path)
    for module in module_root.rglob('*.ko'):
        if '/'+str(module.relative_to(root)) not in wanted:module.unlink()
    for p in ['bin/dd','bin/gzip']:
        shutil.copy(BASE/'analysis/stock-tools'/p,root/p)
        (root/p).chmod(0o755)
    shutil.copy(BUILD/'buttons',root/'buttons')
    shutil.copy(BASE/'antix/installer-init.sh',root/'init'); (root/'init').chmod(0o755)
    for p in ['font-large.psf','font-tall.psf']: shutil.copy(BASE/'antix/assets'/p,root/p)
    os.mknod(root/'dev/console',0o20600,os.makedev(5,1))
    os.mknod(root/'dev/null',0o20666,os.makedev(1,3))
    return root

def initrd(root,output):
    assert (root/"init").stat().st_mode & 0o111, "Installer /init must be executable"
    with output.open('wb') as dest:
        process=subprocess.Popen(['sh','-c','find . -print0 | sort -z | cpio --null -o -H newc --owner=0:0 --quiet'],cwd=root,stdout=subprocess.PIPE)
        with gzip.GzipFile(filename='',mode='wb',fileobj=dest,mtime=0) as packed:
            shutil.copyfileobj(process.stdout,packed,4*MIB)
        assert process.wait()==0

def disk(name,size,files,rootfs=None):
    part=BUILD/(name+'-fat.img')
    fat_mib=63 if rootfs else size-1
    with part.open('wb') as f:f.truncate(fat_mib*MIB)
    run('mkfs.vfat','-F','32','-n','READER',str(part),stdout=subprocess.DEVNULL)
    run('mcopy','-s','-i',str(part),*map(str,files.iterdir()),'::/')
    run('syslinux','--install',str(part))
    result=OUT/(name+'.img')
    with result.open('wb') as f:f.truncate(size*MIB)
    layout=f'label: dos\nunit: sectors\n\nstart=2048,size={fat_mib*2048},type=c,bootable\n'
    if rootfs:layout+=f'start={64*2048},size={(size-64)*2048},type=83\n'
    run('sfdisk',str(result),input=layout.encode(),stdout=subprocess.DEVNULL)
    with result.open('r+b') as dest,part.open('rb') as source:
        dest.write(Path('/usr/lib/syslinux/mbr/mbr.bin').read_bytes())
        dest.seek(MIB);shutil.copyfileobj(source,dest,4*MIB)
        if rootfs:
            dest.seek(64*MIB)
            with rootfs.open('rb') as source:shutil.copyfileobj(source,dest,4*MIB)
    return result

def boot_files(name,ramroot,message):
    files=BUILD/(name+'-files')
    if files.exists():shutil.rmtree(files)
    (files/'boot').mkdir(parents=True)
    shutil.copy(BASE/'analysis/stock-boot/vmlinuz-2.6.27-4-lpia',files/'boot/kernel')
    initrd(ramroot,files/'boot/initrd.gz')
    (files/'syslinux.cfg').write_text('''DEFAULT normal
PROMPT 1
TIMEOUT 30
TOTALTIMEOUT 30
ONTIMEOUT normal
SERIAL 0 115200
DISPLAY boot.msg
LABEL normal
 KERNEL /boot/kernel
 INITRD /boot/initrd.gz
 APPEND console=tty0 loglevel=3
LABEL serial
 KERNEL /boot/kernel
 INITRD /boot/initrd.gz
 APPEND console=ttyS0,115200 loglevel=3
''')
    shutil.copy('/usr/lib/syslinux/modules/bios/ldlinux.c32',files/'ldlinux.c32')
    (files/'boot.msg').write_text(message+'\nPress Enter to boot.\n')
    return files

