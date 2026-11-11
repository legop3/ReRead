#!/usr/bin/env python3
"""Collect .deb assets from published stable GitHub releases using gh auth."""
import argparse
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--repo', required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
pages = json.loads(subprocess.check_output(
    ['gh', 'api', '--paginate', '--slurp', f'repos/{args.repo}/releases'], text=True))
(args.output.parent / 'releases.json').write_text(json.dumps([release for page in pages for release in page], indent=2) + '\n')
count = 0
for releases in pages:
    for release in releases:
        if release['draft'] or release['prerelease']:
            continue
        for asset in release['assets']:
            if not asset['name'].endswith('.deb'):
                continue
            # Asset IDs avoid collisions when an unchanged app appears in several releases.
            directory = args.output / str(asset['id'])
            directory.mkdir(exist_ok=True)
            with (directory / asset['name']).open('wb') as target:
                subprocess.run(['gh', 'api', '-H', 'Accept: application/octet-stream',
                                f'repos/{args.repo}/releases/assets/{asset["id"]}'],
                               stdout=target, check=True)
            count += 1
if not count:
    raise SystemExit('No published stable .deb release assets found')
print(f'Downloaded {count} published package assets')
