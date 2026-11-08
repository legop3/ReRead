"""ReRead release/app inventory and one persistent package-management job."""
import fcntl
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time

STATE = Path('/var/lib/reader')
STAGING = STATE / 'updates'
JOB = STATE / 'package-job.json'
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
    return dict(version=version(), apps=inventory(), job=job, log=log,
                reboot_required=(STATE / 'reboot-required').exists())


def write_job(data):
    STATE.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode='w', dir=STATE, delete=False) as dest:
        json.dump(data, dest)
        path = Path(dest.name)
    os.replace(path, JOB)


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


def start(action, value, kind='app'):
    """The lock covers job handoff; the child inherits it for the operation."""
    STATE.mkdir(parents=True, exist_ok=True)
    LOG.parent.mkdir(parents=True, exist_ok=True)
    with (STATE / 'package.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError('A package operation is already running.') from None
        if action == 'install':
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
