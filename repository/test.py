#!/usr/bin/env python3
"""Exercise a built archive with real APT, including signature rejection."""
import argparse
import functools
import hashlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import subprocess
import tempfile
import threading


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def test(site):
    site = site.resolve()
    handler = functools.partial(QuietHandler, directory=str(site))
    server = ThreadingHTTPServer(('127.0.0.1', 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            for name in ('lists', 'bad-lists'):
                (work / name / 'partial').mkdir(parents=True)
            source = work / 'test.sources'
            key = Path(__file__).resolve().parent / 'reread-archive-keyring.asc'
            source.write_text(f'Types: deb\nURIs: http://127.0.0.1:{server.server_port}/apt\n'
                              f'Suites: stable\nComponents: main\nArchitectures: i386\nSigned-By: {key}\n')
            options = ['-o', f'Dir::Etc::sourcelist={source}', '-o', 'Dir::Etc::sourceparts=-',
                       '-o', f'Dir::State::lists={work / "lists"}', '-o', 'APT::Sandbox::User=root',
                       '-o', 'APT::Architecture=i386']
            subprocess.run(['apt-get', *options, 'update'], check=True)
            subprocess.run(['apt-get', *options, 'download', 'reread:i386'], cwd=work, check=True)
            downloaded, = work.glob('reread_*.deb')
            original = site / 'apt/pool/main' / downloaded.name
            assert hashlib.sha256(downloaded.read_bytes()).digest() == hashlib.sha256(original.read_bytes()).digest()
            release = site / 'apt/dists/stable/InRelease'
            signed = release.read_bytes()
            try:
                release.write_bytes(signed.replace(b'Origin: ReRead', b'Origin: Forged'))
                result = subprocess.run(['apt-get', *options, '-o', f'Dir::State::lists={work / "bad-lists"}',
                                         'update'], text=True, capture_output=True)
                assert result.returncode != 0 and 'BADSIG' in result.stdout + result.stderr, result
            finally:
                release.write_bytes(signed)
            print('PASS: APT accepted the signed archive, downloaded the indexed package, and rejected tampering')
    finally:
        server.shutdown()
        server.server_close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('site', type=Path)
    test(parser.parse_args().site)
