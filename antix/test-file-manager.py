"""Real upstream HTTP/WebDAV file operations against a disposable shared folder."""
from pathlib import Path
import os
import socket
import subprocess
import sys
import tempfile
import time
from urllib.request import Request, urlopen
import zipfile
import io
from third_party import stage_file_manager

BASE = Path(__file__).resolve().parents[1]


def exercise(address, prefix):
    def request(path, method='GET', body=None, headers=None):
        with urlopen(Request(address + path, data=body, method=method, headers=headers or {}), timeout=20) as response:
            return response.read()
    request(prefix, 'MKCOL')
    try:
        payload = b'ReRead shared files round trip\n' * 10000
        request(prefix + 'original.txt', 'PUT', payload)
        assert request(prefix + 'original.txt') == payload
        request(prefix + 'original.txt', 'MOVE', headers={'Destination': address + prefix + 'renamed.txt'})
        assert request(prefix + 'renamed.txt') == payload
        listing = request(prefix + '?ls')
        assert b'renamed.txt' in listing
        request(prefix + 'nested/', 'MKCOL')
        request(prefix + 'renamed.txt', 'MOVE', headers={'Destination': address + prefix + 'nested/moved.txt'})
        assert request(prefix + 'nested/moved.txt') == payload
        packed = request(prefix + '?zip')
        with zipfile.ZipFile(io.BytesIO(packed)) as archive:
            names = archive.namelist()
            match = next(name for name in names if name.endswith('moved.txt'))
            assert archive.read(match) == payload
        request(prefix + 'nested/moved.txt', 'DELETE')
        print('PASS: create folders, upload/download, rename, move, ZIP download and delete')
    finally:
        request(prefix, 'DELETE')


if __name__ == '__main__':
    if len(sys.argv) == 2:
        # Creates and removes only this uniquely named test directory on a Reader.
        exercise(sys.argv[1].rstrip('/'), '/files/reread-transfer-test-' + str(time.time_ns()) + '/')
    else:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            stage_file_manager(root, BASE)
            shared = root / 'files'; shared.mkdir()
            with socket.socket() as listener:
                listener.bind(('127.0.0.1', 0)); port = listener.getsockname()[1]
            config = (BASE / 'antix/files.conf').read_text().replace('[global]', '[global]\n  wram').replace('@/usr/share/reader/files-head.html', '@' + str(BASE / 'antix/files-head.html')).replace('192.168.7.1', '127.0.0.1').replace('p: 8080', f'p: {port}').replace('/var/lib/reader/files', str(shared)).replace('/var/lib/reader/file-manager', str(root / 'state'))
            for original, name in [('/var/lib/reader/apps','app-data'),('/root/.cache','cache'),('/root/.config','preferences'),('/etc/reread/apps','configuration'),('/usr/lib/reread/apps','programs'),('/usr/share/reread/apps','assets')]:
                directory = root / name; directory.mkdir()
                config = config.replace(original, str(directory))
            (root / 'state/home').mkdir(parents=True)
            (root / 'files.conf').write_text(config)
            environment = dict(os.environ, PYTHONPATH=str(root / 'usr/lib/reread/vendor'),
                               XDG_CONFIG_HOME=str(root / 'config'), PYTHONDONTWRITEBYTECODE='1')
            with (root / 'log').open('w') as log:
                process = subprocess.Popen([sys.executable, '-m', 'copyparty', '-c', str(root / 'files.conf')], env=environment, stdout=log, stderr=log)
                try:
                    address = f'http://127.0.0.1:{port}'
                    for _ in range(100):
                        if process.poll() is not None:
                            raise RuntimeError((root / 'log').read_text())
                        try:
                            urlopen(address, timeout=1).close(); break
                        except OSError:
                            time.sleep(.1)
                    exercise(address, '/files/folder/')
                    assert not list(shared.iterdir()), list(shared.iterdir())
                    print('PASS: public storage contains no file-manager metadata or leftover test files')
                except Exception:
                    print((root / 'log').read_text())
                    raise
                finally:
                    process.terminate(); process.wait(15)
