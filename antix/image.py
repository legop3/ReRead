"""Disk-image helpers and the small USB installer environment."""
import gzip
import os
from pathlib import Path
import shutil
import subprocess
import re

BASE=Path('/work')
BUILD=BASE/'build/antix'
OUT=BASE/'out/antix'
MIB=1024*1024
BOOT_MIB=128
SSD_SECTORS=7821072
ROOT_BYTES=SSD_SECTORS*512-BOOT_MIB*MIB
BUILD.mkdir(parents=True,exist_ok=True)
OUT.mkdir(parents=True,exist_ok=True)

def run(*args,**kwargs): return subprocess.run(args,check=True,**kwargs)
KERNEL='5.10.256-antix.1-486-smp'

def installer_tree():
    """Use the installed distro's tools and matching modules, not stock firmware."""
    root=BUILD/'installer'
    system=BUILD/'root'
    if root.exists(): shutil.rmtree(root)
    for directory in ['bin','sbin','usr/bin','dev','proc','sys','tmp','root','etc']:
        (root/directory).mkdir(parents=True,exist_ok=True)

    def copy(path):
        target=root/path.lstrip('/')
        target.parent.mkdir(parents=True,exist_ok=True)
        # Resolve absolute symlinks inside the target filesystem, not the host.
        source=system/path.lstrip('/')
        while source.is_symlink():
            link=os.readlink(source)
            source=system/link.lstrip('/') if link.startswith('/') else source.parent/link
        shutil.copy(source,target)

    for executable in ['/bin/busybox','/usr/bin/setfont']:
        copy(executable)
        linked=run('chroot',str(system),'ldd',executable,capture_output=True,text=True).stdout
        for library in re.findall(r'(?:=>\s+|^\s*)(/\S+)',linked,re.M):
            copy(library)
    applets=run('chroot',str(system),'/bin/busybox','--list',capture_output=True,text=True).stdout.splitlines()
    for applet in applets:
        if applet!='busybox': (root/'bin'/applet).symlink_to('busybox')
    (root/'sbin/modprobe').symlink_to('/bin/busybox')
    modules=system/'lib/modules'/KERNEL
    destination=root/'lib/modules'/KERNEL
    destination.mkdir(parents=True)
    dependencies={}
    for line in (modules/'modules.dep').read_text().splitlines():
        path,rest=line.split(':',1)
        dependencies[path]=rest.split()
    wanted=set()
    def retain(path):
        if path in wanted:return
        wanted.add(path)
        for dependency in dependencies.get(path,[]):retain(dependency)
    names={'ehci_pci','ehci_hcd','uhci_hcd','xhci_pci','xhci_hcd','usb_storage',
           'nls_utf8','uas','sd_mod','ata_piix','pata_sch','vfat','nls_cp437','nls_iso8859_1',
           'usbhid','hid_generic','atkbd','i8042','evdev','button','fbcon'}
    for path in dependencies:
        if Path(path).stem.replace('-','_') in names:retain(path)
    for path in wanted:copy('/lib/modules/'+KERNEL+'/'+path)
    # BusyBox modprobe uses the text dependency and alias indexes.
    for name in ['modules.alias','modules.softdep','modules.builtin']:
        shutil.copy(modules/name,destination/name)
    (destination/'modules.dep').write_text(''.join(
        path+': '+' '.join(dependencies[path])+'\n' for path in sorted(wanted)))
    shutil.copy(BASE/'antix/installer-init.sh',root/'init');(root/'init').chmod(0o755)
    for font in ['font-large.psf','font-tall.psf']:
        shutil.copy(BASE/'antix/assets'/font,root/font)
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

def disk(name,size,files,rootfs=None,boot_serial=None,build_dir=None,output_dir=None):
    part=(build_dir or BUILD)/(name+'-fat.img')
    fat_mib=BOOT_MIB-1 if rootfs else size-1
    with part.open('wb') as f:f.truncate(fat_mib*MIB)
    run('mkfs.vfat','-F','32','-n','REREADBOOT' if rootfs else 'REREADUSB',
        *(['-i',boot_serial] if boot_serial else []),str(part),stdout=subprocess.DEVNULL)
    run('mcopy','-s','-i',str(part),*map(str,files.iterdir()),'::/')
    run('syslinux','--install',str(part))
    result=(output_dir or OUT)/(name+'.img')
    total_bytes=BOOT_MIB*MIB+rootfs.stat().st_size if rootfs else size*MIB
    with result.open('wb') as f:f.truncate(total_bytes)
    layout=f'label: dos\nunit: sectors\n\nstart=2048,size={fat_mib*2048},type=c,bootable\n'
    if rootfs:layout+=f'start={BOOT_MIB*2048},size={rootfs.stat().st_size//512},type=83\n'
    run('sfdisk',str(result),input=layout.encode(),stdout=subprocess.DEVNULL)
    with result.open('r+b') as dest,part.open('rb') as source:
        dest.write(Path('/usr/lib/syslinux/mbr/mbr.bin').read_bytes())
        dest.seek(MIB);shutil.copyfileobj(source,dest,4*MIB)
        if rootfs:
            dest.seek(BOOT_MIB*MIB)
            with rootfs.open('rb') as source:shutil.copyfileobj(source,dest,4*MIB)
    return result

def boot_files(name,ramroot,message):
    files=BUILD/(name+'-files')
    if files.exists():shutil.rmtree(files)
    (files/'boot').mkdir(parents=True)
    shutil.copy(BUILD/'system-files'/('vmlinuz-'+KERNEL),files/'boot/kernel')
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
 APPEND console=tty0 loglevel=3 i8042.dumbkbd=1
LABEL serial
 KERNEL /boot/kernel
 INITRD /boot/initrd.gz
 APPEND console=ttyS0,115200 loglevel=3 i8042.dumbkbd=1
''')
    shutil.copy('/usr/lib/syslinux/modules/bios/ldlinux.c32',files/'ldlinux.c32')
    (files/'boot.msg').write_text(message+'\nPress Enter to boot.\n')
    return files

