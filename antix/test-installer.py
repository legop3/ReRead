"""Optional end-to-end install to a disposable QEMU SSD; never opens host disks."""
from pathlib import Path
import json
import subprocess
import sys
from vm import VM

BASE=Path('/work');BUILD=BASE/'build/antix';OUT=BASE/'out/antix'
target=BUILD/'installer-test-ssd.img'
with target.open('wb') as disk:disk.truncate(4004388864)
v=VM(['-device','usb-ehci,id=ehci',
      '-drive',f'if=none,id=installer,file={OUT / "reread-installer-usb.img"},format=raw,readonly=on',
      '-device','usb-storage,bus=ehci.0,drive=installer,bootindex=1',
      '-drive',f'if=none,id=ssd,file={target},format=raw',
      '-device','ide-hd,drive=ssd,model=SSDPAPS0004G1,bootindex=2'],
     'installer-test',build=BUILD,out=OUT,memory=512)
try:
    v.expect('boot:');v.send('serial')
    v.expect('Image found on',timeout=180)
    v.expect('Automatically installing to')
    v.expect('\nINSTALLATION COMPLETE',timeout=360)
    v.expect('Rebooting in 5 seconds.')
    assert v.p.wait(timeout=30)==0, 'Installer did not reboot'
finally:
    v.close()
layout=json.loads(subprocess.check_output(['sfdisk','--json',str(target)]))['partitiontable']
root=layout['partitions'][1]
assert root['start']+root['size']==target.stat().st_size//512, 'Unallocated SSD tail'
print('Whole SSD allocated; root partition reaches the final sector.',flush=True)
print('Automatic USB discovery, streamed SSD installation and reboot passed.',flush=True)
subprocess.run([sys.executable,str(BASE/'antix/test.py'),'--image',str(target)],check=True)
