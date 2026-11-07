"""Exercise installer boots, source-disk refusal, and installation in QEMU."""
import json
import os
from pathlib import Path
import select
import subprocess
import time

BASE = Path('/work')
OUT = BASE / 'out'
BOOT = BASE / 'build/installer-files/boot'

class VM:
    def __init__(self, args, logfile):
        self.log = open(logfile, 'wb')
        self.process = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.pending = b''

    def expect(self, text, timeout=120):
        expected = text.encode()
        deadline = time.monotonic() + timeout
        while expected not in self.pending:
            if time.monotonic() > deadline:
                raise TimeoutError(text)
            if select.select([self.process.stdout], [], [], 1)[0]:
                chunk = os.read(self.process.stdout.fileno(), 65536)
                if not chunk:
                    raise RuntimeError('VM exited before: ' + text)
                self.log.write(chunk)
                self.log.flush()
                self.pending += chunk
        self.pending = self.pending.split(expected, 1)[1]

    def send(self, text):
        self.process.stdin.write(text.encode() + b'\n')
        self.process.stdin.flush()

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
            self.process.wait(timeout=15)
        self.log.close()

target = BASE / 'build/vm-installed-reader.img'
with target.open('wb') as stream:
    stream.truncate(4004388864)

results = {}
for flavor in ['modern', 'stock']:
    args = ['qemu-system-i386', '-machine', 'accel=kvm:tcg', '-m', '512', '-cpu', 'qemu32', '-display', 'none',
            '-serial', 'stdio', '-monitor', 'none', '-no-reboot', '-usb',
            '-device', 'usb-hub,bus=usb-bus.0,port=1',
            '-device', 'usb-kbd,bus=usb-bus.0,port=1.1',
            '-drive', f'file={OUT}/reader-installer-usb.img,format=raw,if=none,id=media,readonly=on',
            '-device', 'usb-storage,drive=media,bus=usb-bus.0,port=1.2,bootindex=1',
            '-drive', f'file={target},format=raw,if=none,id=target',
            '-device', 'ide-hd,drive=target,bus=ide.0,unit=0,model=SSDPAPS0004G1,bootindex=2']
    vm = VM(args, OUT / ('test-installer-' + flavor + '.log'))
    try:
        vm.expect('boot:')
        vm.send(flavor + '-serial')
        vm.expect('Installer media:')
        vm.expect('Reader installer> ')
        vm.send('disks')
        vm.expect('SSDPAPS0004G1')
        vm.expect('Reader installer> ')
        results[flavor + '_bios_usb_boot_and_hub_detection'] = 'passed'
        print(flavor + ': RAM installer found USB media through emulated hub.', flush=True)
        if flavor == 'stock':
            vm.send('install')
            vm.expect('Enter the whole internal disk path', timeout=240)
            vm.send('/dev/sdb')
            vm.expect('Refused: target must be the known internal SSD')
            vm.expect('Reader installer> ')
            results['source_disk_refusal'] = 'passed'
            with target.open('rb') as stream:
                assert stream.read(1024 * 1024) == bytes(1024 * 1024)
            vm.send('install')
            vm.expect('Enter the whole internal disk path', timeout=240)
            vm.send('/dev/sda')
            vm.expect('type exactly: INSTALL /dev/sda')
            # No automatic installation: disk must still be blank at confirmation.
            with target.open('rb') as stream:
                assert stream.read(1024 * 1024) == bytes(1024 * 1024)
            vm.send('INSTALL /dev/sda')
            vm.expect('INSTALLATION VERIFIED.', timeout=600)
            results['install_and_full_readback'] = 'passed'
            print('stock: explicit installation and full readback passed.', flush=True)
        vm.send('poweroff')
        vm.process.wait(timeout=30)
    finally:
        vm.close()
(OUT / 'installer-test-results.json').write_text(json.dumps(results, indent=2) + '\n')
