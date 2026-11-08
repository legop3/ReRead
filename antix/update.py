#!/usr/bin/python3
"""Detached worker: package operations survive a management-server restart."""
import argparse
import fcntl
import os
from pathlib import Path
import re
import subprocess
import time
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
    parser.add_argument('action', choices=['install', 'remove', 'enable', 'disable'])
    parser.add_argument('value')
    parser.add_argument('--package')
    parser.add_argument('--lock-fd', type=int)
    args = parser.parse_args()
    if args.package is None:
        if args.action == 'install':
            result = subprocess.run(['dpkg-deb', '-f', args.value, 'Package'], text=True, capture_output=True, check=True)
            args.package = result.stdout.strip()
        else:
            args.package = args.value
    if args.package != 'reread' and not re.fullmatch(manager.APP_PATTERN, args.package):
        raise ValueError('Choose a ReRead package.')
    if args.action != 'install' and args.package == 'reread':
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
    try:
        if args.action == 'install':
            manager.package_info(args.value, 'platform' if args.package == 'reread' else 'app')
            run('apt-get', '-y', '--no-remove', '--reinstall', '-o', 'Dpkg::Options::=--force-confdef', '-o', 'Dpkg::Options::=--force-confold',
                'install', str(Path(args.value).resolve()))
            if args.package != 'reread':
                (manager.STATE / 'apps' / args.package.removeprefix('reread-app-')).mkdir(parents=True, exist_ok=True)
                if Path('/etc/runit/runsvdir/default', args.package).is_symlink():
                    subprocess.run(['sv', '-w', '5', 'restart', args.package], check=False)
        elif args.action == 'remove':
            app = next((a for a in manager.inventory() if a['package'] == args.package), None)
            if app and app['service']:
                service(args.package, False)
            run('dpkg', '--remove', args.package)
        else:
            service(args.package, args.action == 'enable')
        job.update(state='complete', message='Complete. Your files and settings were preserved.')
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(str(error), flush=True)
        job.update(state='failed', message='Operation failed. See the log below; files were not reset.')
    finally:
        if args.action == 'install' and Path(args.value).resolve().parent == manager.STAGING.resolve():
            Path(args.value).unlink(missing_ok=True)
        job['finished'] = time.time()
        manager.write_job(job)
    return 0 if job['state'] == 'complete' else 1


if __name__ == '__main__':
    raise SystemExit(main())
