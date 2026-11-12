#!/usr/bin/env python3
"""Repack an existing ReRead USB installer with an opaque full-disk dump."""
import argparse
import gzip
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import image

MIB = 1024 * 1024
SSD_BYTES = 7821072 * 512


def run(*args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


def pack(source, template, output):
    source, template, output = map(Path, (source, template, output))
    if output.resolve() in (source.resolve(), template.resolve()):
        raise ValueError('Output must differ from both input files')
    if output.exists():
        raise ValueError('Output already exists; choose another filename or remove it first')
    with source.open('rb') as stream:
        compressed = stream.read(2) == b'\x1f\x8b'
    if compressed:
        # Normal single-member gzip dumps carry the original size in their footer.
        # The Reader SSD is smaller than gzip's 4 GiB size-field wraparound.
        with source.open('rb') as stream:
            stream.seek(-4, 2)
            size = struct.unpack('<I', stream.read(4))[0]
        with gzip.open(source, 'rb') as stream:
            signature = stream.read(512)
    else:
        size = source.stat().st_size
        with source.open('rb') as stream:
            signature = stream.read(512)
    if not 512 <= size <= SSD_BYTES or size % 512:
        raise ValueError('Expected a sector-aligned disk dump no larger than the Reader SSD (4,004,388,864 bytes)')
    if signature[510:512] != b'\x55\xaa':
        raise ValueError('Expected a bootable whole-disk dump with an MBR, not a filesystem or archive')
    layout = json.loads(subprocess.check_output(['sfdisk', '--json', str(template)]))['partitiontable']
    offset = layout['partitions'][0]['start'] * layout.get('sectorsize', 512)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='reread-restore-', dir=output.parent) as temporary:
        work = Path(temporary)
        files = work / 'files';files.mkdir()
        (files / 'reread').mkdir()
        fat_source = str(template) + '@@' + str(offset)
        # Keep the matching kernel, initramfs and payload marker together. The
        # marker identifies the USB, not the OS or UUID inside the supplied dump.
        for name in ('boot', 'syslinux.cfg', 'ldlinux.c32'):
            run('mcopy', '-s', '-i', fat_source, '::/' + name, files)
        run('mcopy', '-i', fat_source, '::/reread/payload.id', files / 'reread/payload.id')
        if not (files / 'boot/initrd.gz').is_file() or not (files / 'boot/kernel').is_file():
            raise ValueError('Template is not a ReRead USB installer')
        (files / 'boot.msg').write_text('Automatic disk-image restore\nThis erases the internal Reader SSD.\n')
        payload = files / 'reread/reread.img.gz'
        if compressed:
            shutil.copyfile(source, payload)
        else:
            print('Compressing the disk dump for USB storage...', flush=True)
            with source.open('rb') as original, payload.open('wb') as target:
                with gzip.GzipFile(filename='', fileobj=target, mode='wb', compresslevel=1, mtime=0) as packed:
                    shutil.copyfileobj(original, packed, 4 * MIB)
        if payload.stat().st_size >= 0xffffffff:
            raise ValueError('Compressed payload exceeds the FAT filesystem file limit')
        mib = max(128, (sum(p.stat().st_size for p in files.rglob('*') if p.is_file()) + MIB - 1) // MIB + 32)
        result = image.disk('restore', mib, files, build_dir=work, output_dir=work)
        result.rename(output)
    print(f'Restore installer: {output} ({mib} MiB). Flash it with make-usb.sh. The Reader SSD will be erased.', flush=True)
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--template', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:pack(args.image, args.template, args.output)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:parser.exit(1, str(error) + '\n')
