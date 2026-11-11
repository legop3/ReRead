"""Package the current antiX SSD image for the automatic USB installer."""
import json
from pathlib import Path
import shutil
import subprocess
import image
from apps.package import DEFAULT_APPS

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
    app_packages = []
    for name in DEFAULT_APPS:
        app_version = subprocess.check_output(['dpkg-query', '--admindir='+str(BUILD/'root/var/lib/dpkg'),
            '-W', '-f=${Version}', 'reread-app-' + name], text=True).strip()
        app_packages.append(f'reread-app-{name}_{app_version}_i386.deb')
    manifest={'product':'ReRead','version':version,'update_package':f'reread_{version}_i386.deb',
              'application_packages':app_packages,
              'base':'antiX 26.1 Core i386','kernel':'5.10.256-antix.1-486-smp',
              'system_image':system.name,'system_bytes':system.stat().st_size,
              'compressed_image':packed.name,'installer_usb_image':usb.name,
              'installer_bytes':usb.stat().st_size,'root_uuid':root_id,
              'root_partition_bytes':image.ROOT_BYTES,
              'root_partition_mib':image.ROOT_BYTES/MIB,'boot_partition_mib':image.BOOT_MIB-1,
              'installer_mode':'automatic stream, sync, reboot',
              'boot_layout':'FAT mounted at /boot; antiX-based installer',
              'hardware_validation':'See analysis/reread-ui-validation.md for device measurements and remaining checks.'}
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (OUT/'SHA256SUMS').unlink(missing_ok=True)
    print(json.dumps(manifest,indent=2),flush=True)

if __name__=='__main__':
    previous=json.loads((OUT/'manifest.json').read_text())
    packed=OUT/previous['compressed_image']
    package(OUT/previous['system_image'],packed,previous['root_uuid'])
