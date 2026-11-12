#!/usr/bin/env python3
"""Make a restore USB image from a stock/other full-disk .img, .dd or .gz dump."""
import argparse
from pathlib import Path
import subprocess
from types import SimpleNamespace
from release import BASE, Builder


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path, help='Full-disk dump; never a partition backup')
    parser.add_argument('--template', type=Path, default=BASE / 'out/antix/reread-installer-usb.img',
                        help='Downloaded ReRead USB installer; defaults to the local build')
    parser.add_argument('--output', type=Path, default=BASE / 'out/restore-installer.img')
    parser.add_argument('--container', help='Reuse a development builder with this checkout mounted at /work')
    args = parser.parse_args()
    source, template, output = (p.expanduser().resolve() for p in (args.image, args.template, args.output))
    for path in (source, template):
        if not path.is_file():parser.error(f'File not found: {path}. Use --template for a downloaded ReRead installer.')
    if output in (source, template) or output.exists():parser.error('Choose a new output filename; input files are never overwritten')
    output.parent.mkdir(parents=True, exist_ok=True)
    if args.container:
        try:paths = ['/work/' + str(path.relative_to(BASE)) for path in (source, template, output)]
        except ValueError:parser.error('--container requires input/output files inside the checkout; omit it for external paths')
        builder = Builder(SimpleNamespace(container=args.container, image='reread-builder'))
        builder.run('python3', '/work/antix/restore.py', paths[0], '--template', paths[1], '--output', paths[2])
    else:
        Builder(SimpleNamespace(container=None, image='reread-builder'))
        subprocess.run(['docker', 'run', '--rm', '-v', f'{BASE}:/work:z',
                        '-v', f'{source.parent}:/input:ro,z', '-v', f'{template.parent}:/template:ro,z',
                        '-v', f'{output.parent}:/output:z', 'reread-builder', 'python3', '/work/antix/restore.py',
                        '/input/' + source.name, '--template', '/template/' + template.name,
                        '--output', '/output/' + output.name], check=True)


if __name__ == '__main__':
    try:main()
    except subprocess.CalledProcessError as error:raise SystemExit(error.returncode)
    except OSError as error:raise SystemExit(str(error))
