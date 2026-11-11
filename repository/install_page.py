#!/usr/bin/env python3
"""Generate the permanent install page from completed public installer releases."""
import argparse
import html
import json
from pathlib import Path

IMAGE = 'reread-installer-usb.img'


def latest_installer(releases):
    candidates = []
    for release in releases:
        if release['draft'] or release['prerelease']:
            continue
        assets = {asset['name']: asset for asset in release['assets']}
        if IMAGE not in assets or IMAGE + '.sha256' not in assets:
            continue
        candidates.append((release, assets))
    if not candidates:
        raise ValueError('No completed stable installer release with an image and checksum')
    release, assets = max(candidates, key=lambda pair: pair[0]['published_at'])
    return {'tag': release['tag_name'], 'published_at': release['published_at'],
            'release_url': release['html_url'], 'image_url': assets[IMAGE]['browser_download_url'],
            'checksum_url': assets[IMAGE + '.sha256']['browser_download_url'],
            'bytes': assets[IMAGE]['size']}


def build(releases, output):
    latest = latest_installer(releases)
    destination = Path(output) / 'install'
    destination.mkdir(parents=True, exist_ok=True)
    (destination / 'latest.json').write_text(json.dumps(latest, indent=2) + '\n')
    escape = lambda key: html.escape(str(latest[key]), quote=True)
    (destination / 'index.html').write_text('''<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Install ReRead</title>
<style>body{font:17px/1.5 system-ui,sans-serif;max-width:52rem;margin:2rem auto;padding:0 1rem;color:#181818;background:#fff}a{color:#0649b5}li{margin:.5rem 0}code{background:#eee;padding:.1rem .25rem}</style>
<h1>Install ReRead</h1>
<p>A multipurpose handheld Linux platform for the Intel Reader.</p>
<p><strong><a href="''' + escape('image_url') + '''">Download the USB installer</a></strong>
 · <a href="''' + escape('checksum_url') + '''">SHA-256 checksum</a>
 · <a href="''' + escape('release_url') + '''">''' + escape('tag') + ''' release</a></p>
<p>Published ''' + escape('published_at')[:10] + '''. This page always links to the newest completed stable installer.
Application-only releases do not change the installer download.</p>
<h2>Installation</h2>
<p>You need a 1 GB or larger USB stick, a powered USB hub and a USB keyboard.
<strong>Installation erases the Reader's internal SSD.</strong> Save your files first.</p>
<ol><li>Download <code>reread-installer-usb.img</code>.</li>
<li>Write it to the USB stick using <a href="https://etcher.balena.io/">balenaEtcher</a>
(Flash from file → Select target → Flash), or another disk-image writer. This erases the stick.</li>
<li>Connect the stick and keyboard through the powered hub. Press F11 as the Reader starts and select the USB drive.</li>
<li>Installation runs automatically. Keep USB connected until <strong>INSTALLATION COMPLETE</strong>,
then remove it during the reboot countdown.</li></ol>
<p>No source checkout, Docker or stock disk dump is required. Connect Wi-Fi after installation;
Software manages platform and app updates without reinstalling or erasing your files.</p>
<p><a href="https://github.com/legop3/ReRead">Source and documentation</a> ·
<a href="../">APT repository</a></p></html>
''')
    return latest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--catalog', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(build(json.loads(args.catalog.read_text()), args.output)))
