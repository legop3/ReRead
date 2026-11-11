"""Build the static management page; nothing runs on the Reader at build time."""
import hashlib
from pathlib import Path
import re
import tarfile
from urllib.request import urlopen

VERSION = '1.7.4'
ARCHIVE_SHA256 = 'ccb1631d26cec2589d357556c8fe86bbba75ef0bcf672a4939a7ed5d4ab7319c'


def render(root, base):
    archive = base / 'downloads' / f'tabler-core-{VERSION}.tgz'
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        with urlopen(f'https://registry.npmjs.org/@tabler/core/-/core-{VERSION}.tgz', timeout=60) as src:
            data = src.read()
        if hashlib.sha256(data).hexdigest() != ARCHIVE_SHA256:
            raise ValueError('Tabler checksum mismatch')
        archive.write_bytes(data)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != ARCHIVE_SHA256:
        raise ValueError('Tabler checksum mismatch')
    with tarfile.open(archive) as source:
        css = source.extractfile('package/dist/css/tabler.min.css').read().decode()
        js = source.extractfile('package/dist/js/tabler.min.js').read().decode()
    # Only local assets and system fonts: no external font stylesheet or source map.
    css = re.sub(r'@import\s+[^;]+;', '', css)
    css = re.sub(r'/\*# sourceMappingURL=.*?\*/', '', css)
    js = re.sub(r'//# sourceMappingURL=.*', '', js)
    html = (base / 'antix/web.html').read_text()
    html = html.replace('<!-- TABLER_CSS -->', '<style>'+css+'</style>')
    html = html.replace('/* REREAD_LAYOUT_CSS */', (base / 'antix/web/layout.css').read_text())
    html = html.replace('<!-- TABLER_JS -->', '<script>'+js.replace('</script', '<\\/script')+'</script>')
    page = root / 'usr/share/reader/web.html'
    page.parent.mkdir(parents=True, exist_ok=True)
    page.write_text(html)
    for library in ('tabler', 'bootstrap', 'popper'):
        notice = root / f'usr/share/doc/reread/{library}-LICENSE.txt'
        notice.parent.mkdir(parents=True, exist_ok=True)
        notice.write_text((base / f'antix/web/{library}-LICENSE.txt').read_text())


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('root', type=Path)
    args = parser.parse_args()
    render(args.root, Path(__file__).resolve().parents[1])
