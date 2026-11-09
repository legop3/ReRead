#!/usr/bin/python3
"""Detached worker: package operations survive a management-server restart."""
import argparse
import fcntl
import os
from pathlib import Path
import re
import subprocess
import time
import dbus
import apt_pkg
import reader_manager as manager


def run(*args):
    subprocess.run(args, check=True, env={**os.environ, 'DEBIAN_FRONTEND': 'noninteractive'})


def service(package, enabled):
    app = next((a for a in manager.inventory() if a['package'] == package), None)
    if not app or not app['service']:
        raise ValueError('This app does not register a managed service.')
    link = Path('/etc/runit/runsvdir/default', package)
    if enabled:
        if not link.is_symlink():
            link.symlink_to('/etc/sv/' + package)
        # runsvdir discovers the link and starts the service automatically.
    else:
        if link.is_symlink():
            subprocess.run(['sv', '-w', '5', 'down', str(link)], check=False)
            link.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['check', 'online', 'install', 'remove', 'enable', 'disable'])
    parser.add_argument('value', nargs='?', default='')
    parser.add_argument('--package')
    parser.add_argument('--lock-fd', type=int)
    args = parser.parse_args()
    if args.package is None:
        if args.action == 'install':
            result = subprocess.run(['dpkg-deb', '-f', args.value, 'Package'], text=True, capture_output=True, check=True)
            args.package = result.stdout.strip()
        else:
            args.package = args.value
    if args.action != 'check' and args.package != 'reread' and not re.fullmatch(manager.APP_PATTERN, args.package):
        raise ValueError('Choose a ReRead package.')
    if args.action not in ('install', 'online') and args.package == 'reread':
        raise ValueError('Use the app manager only for app actions.')
    lock = None
    if args.lock_fd is None:
        manager.STATE.mkdir(parents=True, exist_ok=True)
        lock = (manager.STATE / 'package.lock').open('a')
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    # Wait for the parent to publish the job before we can complete it.
    if args.lock_fd is not None:
        for _ in range(100):
            if manager.JOB.exists():
                import json
                if json.loads(manager.JOB.read_text()).get('pid') == os.getpid():
                    break
            time.sleep(.02)
    job = dict(state='running', action=args.action, package=args.package, pid=os.getpid(), started=time.time())
    manager.write_job(job)
    inhibitor = None
    try:
        # An offline image/chroot has no system bus or running sleep policy.
        # On a booted Reader, failure to acquire the inhibitor fails the job.
        if Path('/run/dbus/system_bus_socket').exists():
            login = dbus.Interface(dbus.SystemBus().get_object('org.freedesktop.login1', '/org/freedesktop/login1'),
                                   'org.freedesktop.login1.Manager')
            inhibitor = login.Inhibit('idle', 'ReRead package manager', 'Package operation in progress', 'block').take()
        if args.action == 'check':
            with manager.repositories.app_sources() as options:
                arguments = [value for key, option in options.items() for value in ('-o', key + '=' + option)]
                run('apt-get', *arguments, '-o', 'APT::Get::List-Cleanup=false',
                    '-o', 'APT::Update::Error-Mode=any', '-o', 'Acquire::Retries=0',
                    '-o', 'Acquire::http::Timeout=20', '-o', 'Acquire::https::Timeout=20', 'update')
            manager.refresh_catalog(checked=True)
        elif args.action == 'online':
            candidate = next((p for p in manager.available_packages() if p['package'] == args.package), None)
            if not candidate:
                raise ValueError('Package unavailable. Check the ReRead repository first.')
            if candidate['installed'] and not candidate['upgrade']:
                raise ValueError('This package is already up to date.')
            run('apt-get', '-y', '--no-remove', '-o', 'Dpkg::Options::=--force-confdef',
                '-o', 'Dpkg::Options::=--force-confold', 'install', args.package + '=' + candidate['version'])
        elif args.action == 'install':
            manager.package_info(args.value, 'platform' if args.package == 'reread' else 'app')
            if args.package != 'reread':
                manager.check_app_icons(args.value)
            run('apt-get', '-y', '--no-remove', '--reinstall', '-o', 'Dpkg::Options::=--force-confdef', '-o', 'Dpkg::Options::=--force-confold',
                'install', str(Path(args.value).resolve()))
        elif args.action == 'remove':
            app = next((a for a in manager.inventory() if a['package'] == args.package), None)
            if app and app['service']:
                service(args.package, False)
            run('dpkg', '--remove', args.package)
        else:
            service(args.package, args.action == 'enable')
        if args.action in ('install', 'online') and args.package != 'reread':
            (manager.STATE / 'apps' / args.package.removeprefix('reread-app-')).mkdir(parents=True, exist_ok=True)
            if Path('/etc/runit/runsvdir/default', args.package).is_symlink():
                subprocess.run(['sv', '-w', '5', 'restart', args.package], check=False)
        if args.action in ('install', 'online', 'remove'):
            manager.refresh_catalog()
        job.update(state='complete', message='Repository checked.' if args.action == 'check'
                   else 'Complete. Your files and settings were preserved.')
    except (OSError, ValueError, RuntimeError, apt_pkg.Error, subprocess.SubprocessError, dbus.DBusException) as error:
        print(str(error), flush=True)
        job.update(state='failed', message='Repository check failed. Previous results are still shown; see the log below.'
                   if args.action == 'check' else 'Operation failed. See the log below; files were not reset.')
    finally:
        if inhibitor is not None:
            os.close(inhibitor)
        if args.action == 'install' and Path(args.value).resolve().parent == manager.STAGING.resolve():
            Path(args.value).unlink(missing_ok=True)
        job['finished'] = time.time()
        manager.write_job(job)
    return 0 if job['state'] == 'complete' else 1


if __name__ == '__main__':
    raise SystemExit(main())
