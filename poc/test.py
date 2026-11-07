"""Automatic USB-storage installer, refusal cases, readback, and installed boot."""
import hashlib
import json
import os
from pathlib import Path
import select
import socket
import subprocess
import time

BASE=Path('/work'); OUT=BASE/'out/poc'; BUILD=BASE/'build/poc'
class VM:
    def __init__(self,args,name,allow_reboot=False):
        self.socket=BUILD/(name+'.sock');self.socket.unlink(missing_ok=True)
        self.log=(OUT/(name+'.log')).open('wb')
        self.p=subprocess.Popen(['qemu-system-i386','-machine','accel=kvm:tcg','-cpu','qemu32',
            '-m','128','-display','none','-serial','stdio','-monitor',f'unix:{self.socket},server=on,wait=off',
            *([] if allow_reboot else ['-no-reboot']),*args],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        self.pending=b''
    def expect(self,value,timeout=120):
        token=value.encode();end=time.monotonic()+timeout
        while token not in self.pending:
            if time.monotonic()>end: raise TimeoutError(value)
            if select.select([self.p.stdout],[],[],1)[0]:
                chunk=os.read(self.p.stdout.fileno(),65536)
                if not chunk: raise RuntimeError('VM exited before '+value)
                self.log.write(chunk);self.log.flush();self.pending+=chunk
        self.pending=self.pending.split(token,1)[1]
    def send(self,value): self.p.stdin.write(value.encode()+b'\n');self.p.stdin.flush()
    def monitor(self,command):
        with socket.socket(socket.AF_UNIX) as s:
            s.connect(str(self.socket));s.recv(4096);s.sendall(command.encode()+b'\n');time.sleep(.5)
    def close(self):
        if self.p.poll() is None:self.p.terminate();self.p.wait(timeout=10)
        self.log.close()

def empty_disk(name):
    path=BUILD/name
    with path.open('wb') as f:f.truncate(4004388864)
    return path

def assert_untouched(path):
    # Check the complete region the installer would overwrite.
    with path.open('rb') as f:
        for _ in range(64):assert f.read(1024*1024)==bytes(1024*1024)

def installer_vm(image,disks,name,hub=False,allow_reboot=False):
    bus='uhci.0' if hub else 'ehci.0'
    controller='piix3-usb-uhci,id=uhci' if hub else 'usb-ehci,id=ehci'
    args=['-device',controller,
          '-drive',f'file={image},format=raw,if=none,id=media,readonly=on']
    # QEMU's hub is USB 1.1; it needs a UHCI controller, not EHCI alone.
    if hub:args+=['-device',f'usb-hub,bus={bus},port=1']
    args+=['-device',f'usb-storage,drive=media,id=usbmedia,bus={bus},port={"1.1" if hub else "1"},bootindex=1']
    for index,(path,model) in enumerate(disks):
        args+=['-drive',f'file={path},format=raw,if=none,id=disk{index}',
               '-device',f'ide-hd,drive=disk{index},bus=ide.{index//2},unit={index%2},model={model},bootindex={index+2}']
    vm=VM(args,name,allow_reboot=allow_reboot)
    try:
        vm.expect('boot:');vm.send('serial')
    except Exception:
        vm.close();raise
    return vm

results=OUT/'test-results.json'
results.unlink(missing_ok=True)
usb=OUT/'reader-poc-installer-usb.img'
# A wrong model must be refused, despite the matching capacity.
wrong=empty_disk('wrong-model-test.img')
vm=installer_vm(usb,[(wrong,'OTHER_SSD')],'installer-wrong-model')
try:
    vm.expect('REFUSED: expected one known internal Reader SSD; found 0.')
    vm.expect('INSTALLER STOPPED. No automatic retry.')
    assert_untouched(wrong)
    print('Wrong-model target refused without writes.',flush=True)
finally:vm.close()

# A matching marker with damaged bytes must not authorize a write.
import shutil
corrupt=BUILD/'corrupt-installer.img'
shutil.copyfile(usb,corrupt)
corrupt_payload=BUILD/'corrupt-payload.gz'
corrupt_payload.write_bytes(b'not the expected gzip payload\n')
subprocess.run(['mcopy','-o','-i',f'{corrupt}@@1048576',str(corrupt_payload),
                '::/reader-poc/reader-poc.img.gz'],check=True)
target=empty_disk('installed-test.img')
vm=installer_vm(corrupt,[(target,'SSDPAPS0004G1')],'installer-corrupt-payload')
try:
    vm.expect('IMAGE CHECK FAILED');vm.expect('INSTALLER STOPPED. No automatic retry.')
    assert_untouched(target)
    print('Corrupt USB payload refused without writes.',flush=True)
finally:vm.close()

missing=BUILD/'missing-installer.img'
shutil.copyfile(usb,missing)
subprocess.run(['mdel','-i',f'{missing}@@1048576','::/reader-poc/reader-poc.img.gz'],check=True)
vm=installer_vm(missing,[(target,'SSDPAPS0004G1')],'installer-missing-payload')
try:
    vm.expect('FAILED: matching USB payload not found.')
    vm.expect('INSTALLER STOPPED. No automatic retry.')
    assert_untouched(target)
    print('Missing USB payload refused without writes.',flush=True)
finally:vm.close()

# Even two matching drives must never be selected arbitrarily.
second=empty_disk('second-matching-test.img')
vm=installer_vm(usb,[(target,'SSDPAPS0004G1'),(second,'SSDPAPS0004G1')],'installer-ambiguous-target')
try:
    vm.expect('REFUSED: expected one known internal Reader SSD; found 2.')
    vm.expect('INSTALLER STOPPED. No automatic retry.')
    assert_untouched(target);assert_untouched(second)
    print('Ambiguous internal targets refused without writes.',flush=True)
finally:vm.close()

# Real install/reboot: match the Reader's single internal SSD and USB hub.
vm=installer_vm(usb,[(target,'SSDPAPS0004G1')],'installer-test',hub=True,allow_reboot=True)
try:
    vm.expect('Image found on ')
    vm.expect('Automatically installing to /dev/sda (SSDPAPS0004G1).')
    vm.expect('INSTALLATION VERIFIED',timeout=180)
    vm.expect('Rebooting in 5 seconds.')
    vm.monitor('device_del usbmedia')
    vm.expect('boot:');vm.send('serial');vm.expect('READER POC - BOOT OK')
    assert_untouched(wrong)
    print('Automatic install from USB behind hub, readback, and reboot into installed system passed.',flush=True)
finally:vm.close()
manifest=json.loads((OUT/'manifest.json').read_text())
with target.open('rb') as f: assert hashlib.sha256(f.read(manifest['system_bytes'])).hexdigest()==manifest['system_sha256']
vm=VM(['-drive',f'file={target},format=raw,if=ide,snapshot=on'], 'installed-boot-test')
try:
    vm.expect('boot:');vm.send('serial');vm.expect('READER POC - BOOT OK')
    vm.monitor('sendkey up')
    vm.expect('Code 103  UP');vm.expect('RELEASED')
    vm.monitor(f'screendump {OUT}/poc-screen.ppm')
    print('Installed system booted without USB; key press/release displayed.',flush=True)
finally:vm.close()
results.write_text(json.dumps({
    'memory_mib':128,'automatic_usb_storage_install_via_hub':'passed',
    'refuse_wrong_model_without_writes':'passed',
    'refuse_corrupt_payload_without_writes':'passed',
    'refuse_missing_payload_without_writes':'passed',
    'refuse_ambiguous_targets_without_writes':'passed',
    'full_readback_and_automatic_reboot_into_installed_system':'passed',
    'installed_boot_without_usb':'passed','key_press_release_display':'passed',
    'reader_hardware':'boot/button POC and storage installer confirmed by user with powered hub; reboot update not tested'},indent=2)+'\n')
