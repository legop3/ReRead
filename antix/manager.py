"""ReRead release/app inventory and one persistent package-management job."""
import fcntl
import json
import os
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile
import time
from contextlib import contextmanager
from gi.repository import GLib

STATE = Path('/var/lib/reader')
STAGING = STATE / 'updates'
JOB = STATE / 'package-job.json'
CATALOG = STATE / 'package-catalog.json'
LOG = Path('/var/log/reader/update.log')
REGISTRY = Path('/usr/share/reread/apps')
APP_PATTERN = r'reread-app-[a-z0-9][a-z0-9+.-]*'


def version():
    path = Path('/usr/share/reader/version')
    return path.read_text().strip() if path.exists() else 'unpackaged'


def inventory():
    result = subprocess.run(['dpkg-query', '-W', '-f=${binary:Package}\t${Version}\t${db:Status-Status}\t${binary:Summary}\n',
                             'reread-app-*'], capture_output=True, text=True, timeout=5)
    apps = []
    for line in result.stdout.splitlines():
        fields = line.split('\t', 3)
        if len(fields) != 4 or fields[2] != 'installed' or not re.fullmatch(APP_PATTERN, fields[0]):
            continue
        package, app_version, _, description = fields
        manifest = REGISTRY / (package + '.json')
        metadata = {}
        if manifest.exists():
            try:
                metadata = json.loads(manifest.read_text())
                if not isinstance(metadata, dict):
                    metadata = {}
            except (ValueError, OSError):
                pass
        service = metadata.get('service') == package and Path('/etc/sv', package, 'run').is_file()
        enabled = Path('/etc/runit/runsvdir/default', package).is_symlink() if service else False
        state = ''
        if service and enabled:
            status = subprocess.run(['sv', 'status', package], text=True, capture_output=True, timeout=3)
            raw = status.stdout.strip()
            state = 'Running' if raw.startswith('run:') else 'Stopped' if raw.startswith('down:') else 'Waiting to start'
        apps.append(dict(package=package, version=app_version, name=metadata.get('name', package.removeprefix('reread-app-')),
                         description=metadata.get('description', description), service=service, enabled=enabled, state=state))
    return apps


def status():
    job = {}
    if JOB.exists():
        try:
            job = json.loads(JOB.read_text())
        except ValueError:
            pass
    if job.get('state') == 'running':
        with (STATE / 'package.lock').open('a') as lock:
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                job.update(state='failed', message='The operation was interrupted. Review the log and retry.')
            except BlockingIOError:
                pass
    log = ''
    if LOG.exists():
        with LOG.open('rb') as source:
            source.seek(max(0, LOG.stat().st_size - 12000))
            log = source.read().decode(errors='replace')
    catalog = json.loads(CATALOG.read_text()) if CATALOG.exists() else dict(checked=None, packages=[])
    return dict(version=version(), apps=inventory(), job=job, log=log, catalog=catalog,
                reboot_required=(STATE / 'reboot-required').exists())


def write_json(destination, data):
    STATE.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode='w', dir=STATE, delete=False) as dest:
        json.dump(data, dest)
        path = Path(dest.name)
    os.replace(path, destination)


def write_job(data):
    write_json(JOB, data)


@contextmanager
def repository_cache():
    """Load ReRead metadata and installed packages, rather than all of Debian."""
    import apt
    import apt_pkg
    options = {'Dir::Etc::sourcelist': 'sources.list.d/reread.sources', 'Dir::Etc::sourceparts': '-'}
    previous = {key: apt_pkg.config.find(key) if apt_pkg.config.exists(key) else None for key in options}
    try:
        for key, value in options.items():
            apt_pkg.config.set(key, value)
        with apt.Cache() as cache:
            yield cache
    finally:
        for key, value in previous.items():
            if value is None:
                apt_pkg.config.clear(key)
            else:
                apt_pkg.config.set(key, value)


def available_packages():
    """Read APT's candidates once per job, never in the web polling path."""
    import apt_pkg
    packages = []
    with repository_cache() as cache:
        for name in cache.keys():
            if name != 'reread' and not name.startswith('reread-app-'):
                continue
            package = cache[name]
            if name != 'reread' and not re.fullmatch(APP_PATTERN, name):
                continue
            candidate = package.candidate
            if not candidate or not any(o.trusted and o.origin == 'ReRead' and o.archive == 'stable'
                                        for o in candidate.origins):
                continue
            installed = package.installed.version if package.installed else None
            packages.append(dict(package=package.name, version=candidate.version,
                                 description=candidate.summary, installed=installed,
                                 upgrade=bool(installed and apt_pkg.version_compare(candidate.version, installed) > 0)))
    return sorted(packages, key=lambda p: p['package'])


def refresh_catalog(checked=False):
    previous = json.loads(CATALOG.read_text()) if CATALOG.exists() else {}
    data = dict(checked=time.time() if checked else previous.get('checked'), packages=available_packages())
    write_json(CATALOG, data)
    return data


def package_info(path, kind):
    result = subprocess.run(['dpkg-deb', '-f', str(path), 'Package', 'Version', 'Architecture'],
                            text=True, capture_output=True, timeout=15)
    if result.returncode:
        raise ValueError('Choose a valid ReRead .deb package.')
    fields = dict(line.split(': ', 1) for line in result.stdout.splitlines() if ': ' in line)
    name = fields.get('Package', '')
    if kind == 'platform' and name != 'reread':
        raise ValueError('Choose a ReRead platform package. Install app packages on the Apps page.')
    if kind == 'app' and not re.fullmatch(APP_PATTERN, name):
        raise ValueError('App package names must start with reread-app-.')
    if kind not in ('platform', 'app') or fields.get('Architecture') not in ('i386', 'all'):
        raise ValueError('This package is not for the ReRead i386 platform.')
    return fields


def check_app_icons(path):
    """Check desktop metadata once before installation, without unpacking an app."""
    process = subprocess.Popen(['dpkg-deb', '--fsys-tarfile', str(path)],
                               stdout=subprocess.PIPE)
    try:
        with tarfile.open(fileobj=process.stdout, mode='r|') as archive:
            for member in archive:
                name = member.name.removeprefix('./')
                if not (member.isfile() and name.startswith('usr/share/applications/')
                        and name.endswith('.desktop')):
                    continue
                desktop = GLib.KeyFile.new()
                text = archive.extractfile(member).read().decode('utf-8')
                desktop.load_from_data(text, len(text.encode('utf-8')), GLib.KeyFileFlags.NONE)
                keys = desktop.get_keys('Desktop Entry')[0] if desktop.has_group('Desktop Entry') else []
                def value(key):
                    return desktop.get_string('Desktop Entry', key) if key in keys else ''
                if (value('X-ReRead') == 'true' and value('Hidden') != 'true'
                        and value('NoDisplay') != 'true' and not value('Icon').strip()):
                    raise ValueError(f'{Path(name).name} needs an Icon= entry and application artwork.')
        if process.wait():
            raise ValueError('Unable to read application desktop entries.')
    except (tarfile.TarError, GLib.Error, UnicodeError) as error:
        raise ValueError('Unable to read application desktop entries.') from error
    finally:
        process.stdout.close()
        if process.poll() is None:
            process.terminate()
        process.wait()


def start(action, value, kind='app'):
    """The lock covers job handoff; the child inherits it for the operation."""
    STATE.mkdir(parents=True, exist_ok=True)
    LOG.parent.mkdir(parents=True, exist_ok=True)
    with (STATE / 'package.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError('A package operation is already running.') from None
        if action == 'check':
            package = ''
            title = 'Check ReRead repository'
        elif action == 'online':
            package = value
            if package != 'reread' and not re.fullmatch(APP_PATTERN, package):
                raise ValueError('Choose a ReRead package.')
            title = 'Install from repository: ' + package
        elif action == 'install':
            fields = package_info(value, kind)
            package = fields['Package']
            title = 'Install ' + package + ' ' + fields['Version']
        else:
            package = value
            if not re.fullmatch(APP_PATTERN, package) or not any(a['package'] == package for a in inventory()):
                raise ValueError('Choose an installed ReRead app.')
            title = action.title() + ' ' + package
        with LOG.open('wb') as output:
            output.write((title + '\n').encode())
            output.flush()
            args = ['/usr/local/sbin/reader-update', action, str(value), '--package', package,
                    '--lock-fd', str(lock.fileno())]
            process = subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT,
                                       start_new_session=True, pass_fds=(lock.fileno(),))
        data = dict(state='running', action=action, package=package, pid=process.pid, started=time.time(), message=title)
        write_job(data)
        # Closing the parent's duplicate keeps the child's inherited lock held.
        return data
