"""Exercise source-file editing with APT and profile edits with real wpa_supplicant."""
from pathlib import Path
import fcntl
import json
import subprocess
import tempfile
import time
import repositories as repos
import wifi


def test_repositories(root):
    repos.SOURCES = root / 'sources.list.d'; repos.SOURCES.mkdir()
    repos.MAIN = root / 'sources.list'
    repos.KEYS = root / 'keyrings'; repos.STATE = root / 'state'
    public = (Path(__file__).resolve().parent.parent / 'repository/reread-archive-keyring.asc').read_text()
    repos.change(dict(action='add', name='example', uri='https://example.invalid/apt', suite='stable', components='main', key=public))
    entry, = repos.inventory()
    assert entry['apps'] and entry['enabled']
    repos.change(dict(action='toggle', id=entry['id'], enabled=False))
    assert not repos.inventory()[0]['enabled']
    repos.change(dict(action='toggle', id=entry['id'], enabled=True))
    path = repos.source_path(entry['id']); before = path.read_text()
    try:
        repos.change(dict(action='save', id=entry['id'], content='not an APT source\n'))
    except ValueError:
        pass
    else:
        raise AssertionError('Malformed source was accepted')
    assert path.read_text() == before
    repos.change(dict(action='save', id=entry['id'], content=before.replace('stable', 'testing'), key=public))
    assert 'Suites: testing' in path.read_text()
    (repos.SOURCES / 'debian.list').write_text('deb http://deb.debian.org/debian bookworm main\n')
    with repos.app_sources() as options:
        assert sorted(p.name for p in Path(options['Dir::Etc::sourceparts']).iterdir()) == [entry['id']]
    with (repos.STATE / 'package.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        try:
            repos.change(dict(action='remove', id=entry['id']))
        except ValueError:
            pass
        else:
            raise AssertionError('Source edited during a package operation')
    repos.change(dict(action='remove', id=entry['id']))
    assert not path.exists()
    print('PASS: source add/edit/toggle/remove, APT rejects malformed edits, app-source scoping and package lock')


def test_wifi(root):
    wifi.CONFIG = root / 'wpa.conf'; wifi.CONTROL = str(root / 'control')
    wifi.CONFIG.write_text(wifi.HEADER + 'ap_scan=0\n')
    wifi.CONFIG.chmod(0o600)
    wifi.interface = lambda: 'lo'
    with (root / 'wpa.log').open('w') as log:
        process = subprocess.Popen(['wpa_supplicant', '-Dnone', '-i', 'lo', '-c', str(wifi.CONFIG), '-C', wifi.CONTROL], stdout=log, stderr=log)
        try:
            for _ in range(50):
                if process.poll() is not None:
                    raise RuntimeError((root / 'wpa.log').read_text())
                if (Path(wifi.CONTROL) / 'lo').exists():
                    break
                time.sleep(.1)
            first = wifi.save(dict(ssid='First test', security='personal', password='example-password', hidden=True, priority=5))
            second = wifi.save(dict(ssid='Second test', security='open', enabled=False, priority=-50))
            assert len(wifi.profiles('lo')) == 2
            psk = wifi.cli('lo', 'get_network', first, 'psk')
            wifi.save(dict(id=first, ssid='First test', security='personal', password='', priority=10, hidden=False))
            assert wifi.cli('lo', 'get_network', first, 'psk') == psk
            assert wifi.profile('lo', first)['priority'] == 10
            wifi.action(dict(id=first, action='connect'))
            assert not next(p for p in wifi.profiles('lo') if p['id'] == second)['enabled']
            assert 'example-password' not in wifi.CONFIG.read_text()
            assert 'psk' not in json.dumps(wifi.status())
            wifi.action(dict(id=second, action='enable'))
            wifi.action(dict(id=first, action='connect'))
            assert all(p['enabled'] for p in wifi.profiles('lo')), wifi.profiles('lo')
            wifi.action(dict(id=second, action='forget'))
            assert len(wifi.profiles('lo')) == 1
            assert wifi.CONFIG.stat().st_mode & 0o777 == 0o600
            wifi.cli('lo', 'reconfigure')
            assert len(wifi.profiles('lo')) == 1
            print('PASS: native Wi-Fi profile persistence, blank-password edits, priorities, connect/fallback flags and targeted removal')
        finally:
            process.terminate(); process.wait(timeout=10)


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='reread-management-test-') as temporary:
        root = Path(temporary)
        test_repositories(root)
        test_wifi(root)
