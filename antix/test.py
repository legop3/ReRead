"""Optional QEMU framebuffer boot/input check; never touches host disks or a Reader."""
import argparse
import json
from pathlib import Path
import time
from vm import VM

BUILD = Path('/work/build/antix')
OUT = Path('/work/out/antix')
manifest = json.loads((OUT / 'manifest.json').read_text())
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--image', type=Path, default=OUT / manifest['system_image'])
args = parser.parse_args()
vm = VM(['-drive', f'file={args.image},format=raw,if=ide,snapshot=on',
         '-vga', 'std', '-device', 'qemu-xhci', '-device', 'usb-kbd'],
        'boot-test', build=BUILD, out=OUT, memory=512)


def screen(name):
    path = BUILD / (name + '.ppm')
    response = vm.monitor('screendump ' + str(path))
    if 'Error' in response:
        raise RuntimeError(response)
    header, dimensions, maximum, pixels = path.read_bytes().split(b'\n', 3)
    assert header == b'P6' and maximum == b'255'
    width, height = map(int, dimensions.split())
    assert len(pixels) == width * height * 3
    return width, height, pixels


def accent(picture, row):
    width, height, pixels = picture
    # Selected-row border inside the safe content area, away from clock/alerts.
    return sum(pixels[(y * width + x) * 3:(y * width + x) * 3 + 3] == b'\x38\xc9\xbb'
               for y in range(69 + row * 84, 74 + row * 84)
               for x in range(30, width - 30))


def wait_screen(name, predicate, timeout=90):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        picture = screen(name)
        if predicate(picture):
            return picture
        time.sleep(1)
    raise TimeoutError('Framebuffer did not reach ' + name)


try:
    vm.expect('boot:')
    # Provide a VM framebuffer for the same LinuxFB UI used on the Reader.
    vm.send('serial vga=0x315')
    vm.expect('ReRead base: antiX Core', timeout=180)
    vm.expect('/boot')
    vm.expect('vfat')
    wait_screen('ui-vm-home', lambda frame: accent(frame, 0) > 100)
    vm.monitor('sendkey down')
    wait_screen('ui-vm-files-selected', lambda frame: accent(frame, 1) > 100)
    vm.monitor('sendkey up')
    wait_screen('ui-vm-camera-selected', lambda frame: accent(frame, 0) > 100)
    vm.monitor('sendkey ret')
    # With no physical camera, the app still opens and presents its unavailable
    # state; only the real Reader can validate acquisition, focus and flash.
    wait_screen('ui-vm-camera-app', lambda frame: accent(frame, 0) == 0
                and frame[2].count(b'\xee\xf3\xf8') > 100)
    vm.monitor('sendkey esc')
    wait_screen('ui-vm-returned-home', lambda frame: accent(frame, 0) > 100)
    for cycle in range(2):
        vm.monitor('system_powerdown')
        deadline = time.monotonic() + 30
        while 'suspended' not in vm.monitor('info status'):
            if time.monotonic() > deadline:
                raise TimeoutError('VM did not suspend')
            time.sleep(.2)
        vm.monitor('system_wakeup')
        time.sleep(2)
        assert 'running' in vm.monitor('info status'), 'Wake caused another suspend'
        wait_screen('ui-vm-resumed-' + str(cycle), lambda frame: accent(frame, 0) > 100)
    print('SSD boot, Qt framebuffer input, app return and two VM suspend/resume cycles: PASS', flush=True)
finally:
    vm.close()
