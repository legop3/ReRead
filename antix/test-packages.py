"""Exercise real package upgrades and app jobs in a disposable chroot, not a Reader."""
import http.client
import json
from pathlib import Path
import shutil
import subprocess
import time
from release import build

BASE = Path('/work')
BUILD = BASE / 'build/antix'
ROOT = BUILD / 'package-test-root'
OUT = BUILD / 'package-tests'


def run(*args):
    return subprocess.run(args, check=True)


def chroot(*args):
    return run('chroot', str(ROOT), *args)


def fixture(version):
    root = OUT / ('app-' + version)
    for name in ['DEBIAN', 'usr/share/reread/apps', 'etc/reread/apps', 'etc/sv/reread-app-test']:
        (root / name).mkdir(parents=True, exist_ok=True)
    (root / 'DEBIAN/control').write_text(f'''Package: reread-app-test
Version: {version}
Architecture: all
Maintainer: ReRead tests
Depends: reread (>= 0.1.0)
Description: Disposable app-management fixture
''')
    (root / 'DEBIAN/conffiles').write_text('/etc/reread/apps/test.conf\n')
    (root / 'etc/reread/apps/test.conf').write_text('default=' + version + '\n')
    (root / 'etc/sv/reread-app-test/run').write_text('#!/bin/sh\nexec sleep 3600\n')
    (root / 'etc/sv/reread-app-test/run').chmod(0o755)
    (root / 'usr/share/reread/apps/reread-app-test.json').write_text(json.dumps(
        dict(name='Test app', description='Disposable fixture', service='reread-app-test')))
    package = OUT / ('reread-app-test_' + version + '_all.deb')
    run('dpkg-deb', '--root-owner-group', '--build', str(root), str(package))
    target = ROOT / 'tmp' / package.name
    shutil.copy(package, target)
    return '/tmp/' + package.name


def server():
    script = """import importlib.machinery, sys
sys.path.insert(0,'/usr/local/sbin')
m=importlib.machinery.SourceFileLoader('web','/usr/local/sbin/reader-web').load_module()
m.ADDRESS='127.0.0.1'
m.ThreadingHTTPServer(('127.0.0.1',18082),m.Handler).serve_forever()
"""
    process = subprocess.Popen(['chroot', str(ROOT), 'python3', '-c', script], stdout=subprocess.DEVNULL)
    for _ in range(100):
        try:
            request('GET', '/api/platform')
            return process
        except (OSError, http.client.HTTPException):
            time.sleep(.1)
    process.terminate()
    raise RuntimeError('Management server did not start')


def request(method, path, data=None):
    connection = http.client.HTTPConnection('127.0.0.1', 18082, timeout=10)
    connection.request(method, path, body=data, headers={'Host': '127.0.0.1'})
    response = connection.getresponse()
    body = response.read()
    connection.close()
    assert response.status in (200, 202), (response.status, body)
    return json.loads(body)


def wait_job():
    for _ in range(300):
        state = request('GET', '/api/platform')
        if state['job']['state'] != 'running':
            assert state['job']['state'] == 'complete', state
            return state
        time.sleep(.2)
    raise TimeoutError('Package operation did not finish')


def main():
    OUT.mkdir(exist_ok=True)
    if ROOT.exists():
        shutil.rmtree(ROOT)
    run('cp', '-a', str(BUILD / 'root'), str(ROOT))
    preserved = {
        'var/lib/reader/files/keep.txt': b'personal file\n',
        'var/lib/reader/apps/example/data.txt': b'app data\n',
        'etc/wpa_supplicant/reader.conf': b'ctrl_interface=DIR=/run/wpa_supplicant\n# saved network fixture\n',
        'etc/hostname': b'personal-hostname\n',
        'etc/dropbear/dropbear_ed25519_host_key': b'synthetic host-key preservation fixture\n',
        'root/.ssh/authorized_keys': b'synthetic authorized-key preservation fixture\n',
        'etc/shadow': (ROOT / 'etc/shadow').read_bytes(),
    }
    for name, data in preserved.items():
        path = ROOT / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    for version in ('0.1.0', '0.1.1'):
        package = build(BASE, BUILD, OUT, version=version)
        target = ROOT / 'tmp' / package.name
        shutil.copy(package, target)
        if version == '0.1.0':
            chroot('dpkg', '--force-confdef', '--force-confold', '-i', '/tmp/' + package.name)
        else:
            chroot('/usr/local/sbin/reader-update', 'install', '/tmp/' + package.name)
        assert (ROOT / 'usr/share/reader/version').read_text().strip() == version
        for name, data in preserved.items():
            assert (ROOT / name).read_bytes() == data, 'Update changed ' + name
    fixture('1.0')
    process = server()
    try:
        request('POST', '/api/apps/install', (ROOT / 'tmp/reread-app-test_1.0_all.deb').read_bytes())
        # The operation must survive the HTTP parent going away.
        process.terminate()
        process.wait(timeout=5)
        process = server()
        state = wait_job()
        assert state['apps'][0]['name'] == 'Test app'
        chroot('/usr/local/sbin/reader-update', 'enable', 'reread-app-test')
        assert (ROOT / 'etc/runit/runsvdir/default/reread-app-test').is_symlink()
        app_config = ROOT / 'etc/reread/apps/test.conf'
        app_config.write_text('personal app configuration\n')
        app_data = ROOT / 'var/lib/reader/apps/test/keep.txt'
        app_data.write_text('personal app data\n')
        newer = fixture('2.0')
        chroot('/usr/local/sbin/reader-update', 'install', newer)
        assert app_config.read_text() == 'personal app configuration\n'
        assert request('GET', '/api/platform')['apps'][0]['version'] == '2.0'
        request('POST', '/api/apps/action', json.dumps(dict(package='reread-app-test', action='remove')).encode())
        state = wait_job()
        assert not state['apps']
        assert app_data.read_text() == 'personal app data\n'
        assert app_config.read_text() == 'personal app configuration\n'
        for name, data in preserved.items():
            assert (ROOT / name).read_bytes() == data, name
        print('PASS: platform upgrade preserves personal files, Wi-Fi, hostname, passwords and SSH keys.', flush=True)
        print('PASS: app install, inventory, enable, upgrade and removal preserve app data/config.', flush=True)
        print('PASS: detached web package job survives HTTP-server restart.', flush=True)
    finally:
        process.terminate()
        process.wait(timeout=5)


if __name__ == '__main__':
    main()
