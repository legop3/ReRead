"""Optional QEMU X11 boot/input check; never touches host disks or a Reader."""
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


def accent(picture, column):
    width, height, pixels = picture
    # The fresh image has Camera, Software, Files and Settings in two grid rows.
    content_height = height - 84
    tile_width = (width - 60) // 3
    tile_height = (content_height - 66) // 2
    top = 70 + (content_height - 50 - (tile_height * 2 + 16)) // 2
    left = 14 + column * (tile_width + 16)
    return sum(pixels[(y * width + x) * 3:(y * width + x) * 3 + 3] == b'\x65\xde\xd0'
               for y in range(top + 1, top + 4)
               for x in range(left + 25, left + tile_width - 25))


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
    # Provide a VM framebuffer for the same X11/fbdev UI used on the Reader.
    vm.send('serial vga=0x315')
    vm.expect('ReRead base: antiX Core', timeout=180)
    vm.expect('/boot')
    vm.expect('vfat')
    wait_screen('ui-vm-home', lambda frame: accent(frame, 0) > 100)
    vm.monitor('sendkey right')
    wait_screen('ui-vm-software-selected', lambda frame: accent(frame, 1) > 100)
    vm.monitor('sendkey ret')
    wait_screen('ui-vm-software-app', lambda frame: accent(frame, 1) == 0
                and frame[2].count(b'\xee\xf3\xf8') > 100)
    vm.monitor('sendkey esc')
    wait_screen('ui-vm-software-returned', lambda frame: accent(frame, 1) > 100)
    vm.monitor('sendkey left')
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
    print('SSD boot, Qt X11 input, app return and two VM suspend/resume cycles: PASS', flush=True)
finally:
    vm.close()
