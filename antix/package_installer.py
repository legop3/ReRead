"""Package the current antiX SSD image for the automatic USB installer."""
import json
from pathlib import Path
import shutil
import image

BASE=Path('/work');BUILD=BASE/'build/antix';OUT=BASE/'out/antix';MIB=1024*1024

def package(system,packed,root_id):
    installer=image.installer_tree()
    shutil.copy(BASE/'antix/install.sh',installer/'install');(installer/'install').chmod(0o755)
    (installer/'image-info').write_text(f'BUILD_ID={root_id}\n')
    files=image.boot_files('installer',installer,'ReRead automatic SSD installer')
    (files/'reread').mkdir()
    shutil.copy(packed,files/'reread/reread.img.gz')
    (files/'reread/payload.id').write_text(root_id+'\n')
    size=max(128,(sum(p.stat().st_size for p in files.rglob('*') if p.is_file())+MIB-1)//MIB+32)
    usb=image.disk('reread-installer-usb',size,files)
    version=(BUILD/'root/usr/share/reader/version').read_text().strip()
    manifest={'product':'ReRead','version':version,'update_package':f'reread_{version}_i386.deb',
              'base':'antiX 26.1 Core i386','kernel':'5.10.256-antix.1-486-smp',
              'system_image':system.name,'system_bytes':system.stat().st_size,
              'compressed_image':packed.name,'installer_usb_image':usb.name,
              'installer_bytes':usb.stat().st_size,'root_uuid':root_id,
              'root_partition_bytes':image.ROOT_BYTES,
              'root_partition_mib':image.ROOT_BYTES/MIB,'boot_partition_mib':image.BOOT_MIB-1,
              'installer_mode':'automatic stream, sync, reboot',
              'boot_layout':'FAT mounted at /boot; antiX-based installer',
              'hardware_status':'0.2.1 full-disk install confirmed on Reader; fresh root reports 2162.8 MiB free of 3561.6 MiB',
              'usb_gadget_status':'reader2: DHCP, HTTP file transfers and SSH confirmed with Fedora',
              'wifi_status':'adapter discovery, scanning, saved network and HTTPS confirmed on Reader'}
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (OUT/'SHA256SUMS').unlink(missing_ok=True)
    print(json.dumps(manifest,indent=2),flush=True)

if __name__=='__main__':
    previous=json.loads((OUT/'manifest.json').read_text())
    packed=OUT/previous['compressed_image']
    package(OUT/previous['system_image'],packed,previous['root_uuid'])
