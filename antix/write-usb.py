"""Linux front end to lsblk/dd for downloaded or built ReRead installers."""
import argparse
import json
import os
from pathlib import Path
import stat
import subprocess

BASE = Path(__file__).resolve().parents[1]


def usb_disks():
    result = subprocess.check_output([
        'lsblk', '--json', '--bytes', '--paths', '--tree',
        '--output', 'PATH,SIZE,MODEL,TRAN,TYPE,MOUNTPOINTS',
    ], text=True)
    return [disk for disk in json.loads(result)['blockdevices']
            if disk['type'] == 'disk' and disk.get('tran') == 'usb']


def validate_target(disk, image_size):
    if disk['type'] != 'disk' or disk.get('tran') != 'usb':
        raise ValueError('Choose a whole USB drive, not a partition or internal disk.')
    if int(disk['size']) < image_size:
        raise ValueError('The USB drive is too small for this installer.')

    def check_mounts(node):
        if any(node.get('mountpoints') or []):
            raise ValueError('Unmount the USB partitions (and disable any swap on it) first.')
        for child in node.get('children', []):
            check_mounts(child)

    check_mounts(disk)


def main():
    parser = argparse.ArgumentParser(
        prog='./make-usb.sh',
        description='Write a ReRead installer to a USB drive on Linux. All data on that drive is erased.',
        epilog='Use ./build.sh to build from source, or ./make-usb.sh --build to build and flash.')
    parser.add_argument('image', nargs='?', type=Path,
                        default=BASE / 'out/antix/reread-installer-usb.img',
                        help='installer .img file (default: out/antix/reread-installer-usb.img)')
    parser.add_argument('--device', type=Path, help='whole USB drive, e.g. /dev/sdb; otherwise choose from a list')
    parser.add_argument('--yes', action='store_true', help='skip confirmation; requires --device')
    args = parser.parse_args()
    if args.yes and not args.device:
        parser.error('--yes requires an explicit --device')
    if not args.image.is_file() or args.image.stat().st_size == 0:
        parser.error(f'Installer image not found or empty: {args.image}. Download it from Releases or run ./build.sh.')
    if os.geteuid() != 0:
        parser.error('Run ./make-usb.sh, which uses sudo for writing the USB drive.')
    disks = usb_disks()
    if not disks:
        raise ValueError('No USB drives found. Connect a USB stick and try again.')
    if args.device:
        device = args.device.resolve(strict=True)
        disk = next((item for item in disks if Path(item['path']) == device), None)
        if disk is None:
            raise ValueError('The selected device is not a whole USB drive.')
    else:
        print('USB drives (all data on the selected drive will be erased):')
        for index, item in enumerate(disks, 1):
            print(f"  {index}. {item['path']}  {item.get('model') or 'USB drive'}  {int(item['size']) / 1e9:.1f} GB")
        choice = input('Drive number (Enter to cancel): ').strip()
        if not choice:
            return
        if not choice.isdecimal() or not 1 <= int(choice) <= len(disks):
            raise ValueError('Invalid drive number.')
        disk = disks[int(choice) - 1]
        device = Path(disk['path'])
    validate_target(disk, args.image.stat().st_size)
    target_stat = device.stat()
    if not stat.S_ISBLK(target_stat.st_mode):
        raise ValueError('The target is not a block device.')
    print(f"Write {args.image} to {device} ({disk.get('model') or 'USB drive'}, {int(disk['size']) / 1e9:.1f} GB).", flush=True)
    if not args.yes and input('Erase this USB drive and continue? [y/N] ').strip().lower() != 'y':
        print('Cancelled. No data written.')
        return
    # Recheck after the prompt; O_EXCL also refuses a device mounted meanwhile.
    current = next((item for item in usb_disks() if item['path'] == str(device)), None)
    if current is None:
        raise ValueError('USB drive was disconnected.')
    validate_target(current, args.image.stat().st_size)
    fd = os.open(device, os.O_WRONLY | os.O_EXCL)
    try:
        if os.fstat(fd).st_rdev != target_stat.st_rdev:
            raise ValueError('USB device changed; select it again.')
        with args.image.open('rb') as source:
            subprocess.run(['dd', 'bs=4M', 'status=progress', 'conv=fsync'],
                           stdin=source, stdout=fd, check=True)
    finally:
        os.close(fd)
    print('USB READY. Boot it on the Reader through a powered hub.')
    print('Installation erases the Reader SSD and reboots automatically. Keep USB attached until INSTALLATION COMPLETE.')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError, EOFError) as error:
        raise SystemExit(str(error))
    except KeyboardInterrupt:
        raise SystemExit('\nCancelled.')
