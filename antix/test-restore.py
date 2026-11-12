"""Restore a disposable raw/gzip fixture through the real USB installer in QEMU."""
import gzip
from pathlib import Path
import subprocess
from restore import pack
from vm import VM

BASE=Path('/work');work=BASE/'build/restore-validation';work.mkdir(parents=True,exist_ok=True)
source=work/'fixture.dd'
data=bytearray(2*1024*1024);data[510:512]=b'\x55\xaa'
marker=b'Opaque disk restore fixture';data[-len(marker):]=marker
source.write_bytes(data)
for name in ('raw-usb.img','gzip-usb.img'):(work/name).unlink(missing_ok=True)
usb=pack(source,BASE/'out/antix/reread-installer-usb.img',work/'raw-usb.img')
packed=work/'fixture.dd.gz'
with gzip.open(packed,'wb') as target:target.write(data)
compressed_usb=pack(packed,BASE/'out/antix/reread-installer-usb.img',work/'gzip-usb.img')
payload=subprocess.check_output(['mtype','-i',str(compressed_usb)+'@@1048576','::/reread/reread.img.gz'])
assert payload==packed.read_bytes(),'Compressed input was changed'
ssd=work/'disposable-ssd.img'
with ssd.open('wb') as target:target.truncate(4004388864)
v=VM(['-device','usb-ehci,id=ehci','-drive',f'if=none,id=usb,file={usb},format=raw,readonly=on',
      '-device','usb-storage,bus=ehci.0,drive=usb,bootindex=1','-drive',f'if=none,id=ssd,file={ssd},format=raw',
      '-device','ide-hd,drive=ssd,model=SSDPAPS0004G1,bootindex=2'],
     'restore-validation',build=work,out=work,memory=512)
try:
    v.expect('boot:');v.send('serial')
    v.expect('Automatically installing to',timeout=180)
    v.expect('INSTALLATION COMPLETE',timeout=120)
    v.expect('Rebooting in 5 seconds.')
    assert v.p.wait(timeout=30)==0,'Restore did not reboot'
finally:v.close()
with ssd.open('rb') as restored:
    assert restored.read(len(data))==data,'Restored bytes differ from supplied image'
print('PASS: raw dump restored byte-for-byte, gzip input retained unchanged, automatic reboot; no physical disks touched.')
