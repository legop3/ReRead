#!/usr/bin/env python3
"""Build a ReRead platform/installer or app release, optionally publish it with gh."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

BASE = Path(__file__).resolve().parent
sys.path.insert(0, str(BASE / 'antix'))
from apps.package import DEFAULT_APPS


def run(*args, capture=False, **kwargs):
    result = subprocess.run([str(a) for a in args], cwd=BASE, check=True,
                            text=True, stdout=subprocess.PIPE if capture else None, **kwargs)
    return result.stdout.strip() if capture else None


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def gh_json(*args):
    return json.loads(run('gh', *args, capture=True))


def releases(repo):
    return [release for page in gh_json('api', '--paginate', '--slurp', f'repos/{repo}/releases?per_page=100&fresh={time.time_ns()}')
            for release in page]


def stable_assets(catalog):
    result = {}
    for release in catalog:
        if release['draft'] or release['prerelease']:
            continue
        for asset in release['assets']:
            # Prefer GitHub's immutable asset ID, not a mutable "latest" URL.
            if asset['name'] not in result:
                result[asset['name']] = asset
    return result


def download(repo, asset, destination):
    destination = Path(destination)
    partial = destination.with_suffix(destination.suffix + '.part')
    try:
        with partial.open('wb') as target:
            subprocess.run(['gh', 'api', '-H', 'Accept: application/octet-stream',
                            f'repos/{repo}/releases/assets/{asset["id"]}'], stdout=target, check=True)
        digest = asset.get('digest')
        if not digest or digest != 'sha256:' + sha(partial):
            raise RuntimeError('Missing/mismatched GitHub SHA-256 digest: ' + asset['name'])
        partial.replace(destination)
    finally:
        partial.unlink(missing_ok=True)


class Builder:
    def __init__(self, args):
        self.container, self.image = args.container, args.image
        if not self.container:
            # Docker's layer cache handles unchanged build dependencies.
            if subprocess.run(['docker', 'buildx', 'inspect', 'reread'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
                run('docker', 'buildx', 'create', '--name', 'reread', '--driver', 'docker-container')
            run('docker', 'buildx', 'build', '--builder', 'reread', '--load', '-t', self.image, 'image-builder')

    def run(self, *command, env=None, capture=False):
        env_args = [item for key, value in (env or {}).items() for item in ('-e', f'{key}={value}')]
        if self.container:
            return run('docker', 'exec', '-w', '/work', *env_args, self.container, *command, capture=capture)
        else:
            return run('docker', 'run', '--rm', '-v', f'{BASE}:/work:z', *env_args, self.image, *command, capture=capture)


def prepare(args, version, tag, commit, catalog):
    bundle = BASE / 'out/releases' / tag
    bundle.mkdir(parents=True, exist_ok=True)
    builder = Builder(args)
    if args.kind == 'app':
        builder.run('python3', '/work/antix/apps/package.py', args.app)
        paths = [BASE / f'out/antix/reread-app-{args.app}_{version}_{arch}.deb' for arch in ('i386', 'all')]
        paths = [path for path in paths if path.is_file()]
        if len(paths) != 1:
            raise RuntimeError('Expected exactly one i386/all app package')
    else:
        packages = BASE / 'build/releases' / tag / 'packages'
        packages.mkdir(parents=True, exist_ok=True)
        names = [f'reread_{version}_i386.deb'] + [
            f'reread-app-{name}_{(BASE / "antix/apps" / name / "VERSION").read_text().strip()}_i386.deb'
            for name in DEFAULT_APPS]
        published = stable_assets(catalog)
        # Never rebuild an already-published package version into different bytes.
        for name in names:
            if name in published:
                download(args.repo, published[name], packages / name)
            else:
                (packages / name).unlink(missing_ok=True)
        builder.run('python3', '/work/antix/build.py', env={
            'REREAD_PACKAGE_DIR': '/work/' + str(packages.relative_to(BASE))})
        paths = [packages / name for name in names]
        paths += [BASE / 'out/antix/reread-installer-usb.img', BASE / 'out/antix/manifest.json']
    for path in paths:
        if path.suffix == '.deb':
            identity = builder.run('dpkg-deb', '--show', '--showformat=${Package}_${Version}_${Architecture}.deb',
                                   '/work/' + str(path.relative_to(BASE)), capture=True)
            if identity != path.name:
                raise RuntimeError('Package identity does not match its filename: ' + path.name)
        shutil.copyfile(path, bundle / path.name)
    image = bundle / 'reread-installer-usb.img'
    if args.kind == 'platform':
        image_manifest = json.loads((bundle / 'manifest.json').read_text())
        if image_manifest['version'] != version or image_manifest['installer_bytes'] != image.stat().st_size:
            raise RuntimeError('Installer manifest does not match this release')
        (bundle / (image.name + '.sha256')).write_text(sha(image) + '  ' + image.name + '\n')
    assets = {p.name: sha(bundle / p.name) for p in paths}
    if args.kind == 'platform':
        assets[image.name + '.sha256'] = sha(bundle / (image.name + '.sha256'))
    metadata = {'kind': args.kind, 'app': getattr(args, 'app', None), 'version': version,
                'tag': tag, 'commit': commit, 'repository': args.repo, 'assets': assets}
    (bundle / 'release.json').write_text(json.dumps(metadata, indent=2) + '\n')
    assets['release.json'] = sha(bundle / 'release.json')
    (bundle / 'SHA256SUMS').write_text(''.join(f'{digest}  {name}\n' for name, digest in sorted(assets.items())))
    return bundle


def verify(bundle, args, commit, tag):
    meta = json.loads((bundle / 'release.json').read_text())
    if meta['commit'] != commit or meta['tag'] != tag or meta['repository'] != args.repo:
        raise RuntimeError('Prepared release belongs to a different source commit, tag or repository; rebuild it')
    for name, digest in meta['assets'].items():
        if sha(bundle / name) != digest:
            raise RuntimeError('Prepared asset changed: ' + name)
    expected = dict(meta['assets'], **{'release.json': sha(bundle / 'release.json')})
    checksum = ''.join(f'{digest}  {name}\n' for name, digest in sorted(expected.items()))
    if (bundle / 'SHA256SUMS').read_text() != checksum:
        raise RuntimeError('Prepared checksum file changed; rebuild it')
    return meta


def publish(args, bundle, meta, catalog):
    tag, commit = meta['tag'], meta['commit']
    existing = next((r for r in catalog if r['tag_name'] == tag), None)
    if existing and not existing['draft']:
        raise RuntimeError('Release is already published; use a new version')
    # Detect reused package versions across every release, not only this tag.
    published = stable_assets(catalog)
    for name, digest in meta['assets'].items():
        if name.endswith('.deb') and name in published and published[name].get('digest') != 'sha256:' + digest:
            raise RuntimeError('Package version already published with different bytes: ' + name)
    if run('git', 'rev-parse', 'HEAD', capture=True) != commit or run('git', 'status', '--porcelain', capture=True):
        raise RuntimeError('Source changed during the build; commit and rebuild before publishing')
    run('git', 'push', 'origin', 'HEAD:main')
    if not existing:
        title = f'ReRead {meta["version"]}' if args.kind == 'platform' else f'{args.app} {meta["version"]}'
        notes = ['--notes-file', args.notes_file] if args.notes_file else ['--generate-notes']
        run('gh', 'release', 'create', tag, '--repo', args.repo, '--draft', '--target', commit,
            '--title', title, *notes)
        catalog = releases(args.repo)
        existing = next(r for r in catalog if r['tag_name'] == tag)
    elif existing['target_commitish'] != commit:
        raise RuntimeError('Draft targets another commit; inspect the draft before resuming')
    paths = [bundle / name for name in meta['assets']] + [bundle / 'release.json', bundle / 'SHA256SUMS']
    for path in paths:
        old = next((a for a in existing['assets'] if a['name'] == path.name), None)
        if old:
            if old.get('digest') != 'sha256:' + sha(path):
                raise RuntimeError('Conflicting draft asset: ' + path.name)
        else:
            run('gh', 'release', 'upload', tag, path, '--repo', args.repo)
    uploaded = next(r for r in releases(args.repo) if r['tag_name'] == tag)
    for path in paths:
        asset = next(a for a in uploaded['assets'] if a['name'] == path.name)
        if asset.get('digest') != 'sha256:' + sha(path):
            raise RuntimeError('Uploaded digest mismatch: ' + path.name)
    since = time.time() - 5
    run('gh', 'release', 'edit', tag, '--repo', args.repo, '--draft=false',
        '--latest=' + ('true' if args.kind == 'platform' else 'false'))
    # The existing release event triggers the signed APT/Pages deployment.
    print('Published assets; waiting for APT/Pages verification...', flush=True)
    for _ in range(30):
        jobs = gh_json('run', 'list', '--repo', args.repo, '--workflow', 'apt-repository.yml',
                       '--limit', '20', '--json', 'databaseId,event,createdAt,headBranch')
        from datetime import datetime
        jobs = [j for j in jobs if j['event'] == 'release' and j['headBranch'] == tag and
                datetime.fromisoformat(j['createdAt'].replace('Z', '+00:00')).timestamp() >= since]
        if jobs:
            job = min(jobs, key=lambda j: j['createdAt'])
            run('gh', 'run', 'watch', job['databaseId'], '--repo', args.repo, '--exit-status', '--interval', '10')
            print('Release and public APT/Pages deployment completed.')
            return
        time.sleep(2)
    raise RuntimeError('Release published, but no APT workflow appeared; inspect GitHub Actions')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kind', choices=('platform', 'app'))
    parser.add_argument('app', nargs='?', help='App directory name (app releases only)')
    parser.add_argument('--publish', action='store_true', help='Push source, upload a draft, verify assets, publish and wait for APT')
    parser.add_argument('--skip-build', action='store_true', help='Use the verified prepared bundle for this exact source commit')
    parser.add_argument('--notes-file', type=Path, help='Release notes; defaults to GitHub generated notes')
    parser.add_argument('--repo', help='GitHub OWNER/REPO; inferred from origin')
    parser.add_argument('--container', help='Reuse a running builder with this checkout mounted at /work')
    parser.add_argument('--image', default='reread-builder', help='Docker builder image')
    args = parser.parse_args()
    if (args.kind == 'app') != bool(args.app) or (args.app and not re.fullmatch('[a-z0-9][a-z0-9-]*', args.app)):
        parser.error('Use "platform" or "app NAME"')
    if not args.repo:
        remote = run('git', 'remote', 'get-url', 'origin', capture=True)
        match = re.fullmatch(r'(?:https://github.com/|git@github.com:)([^/]+/[^/]+?)(?:\.git)?', remote)
        if not match:
            parser.error('Use --repo OWNER/REPO for a non-GitHub origin')
        args.repo = match[1]
    version_file = BASE / ('antix/apps/' + args.app + '/VERSION' if args.app else 'antix/VERSION')
    version = version_file.read_text().strip()
    if not re.fullmatch(r'[0-9][a-zA-Z0-9.+~-]*', version):
        parser.error('Invalid release version')
    tag = f'{args.app}-v{version}' if args.app else f'v{version}'
    commit = run('git', 'rev-parse', 'HEAD', capture=True)
    if args.publish:
        if run('git', 'status', '--porcelain', capture=True):
            parser.error('Commit source changes before publishing so the release identifies its exact source')
        if run('git', 'branch', '--show-current', capture=True) != 'main':
            parser.error('Official releases are published from main')
    catalog = releases(args.repo) if args.publish or args.kind == 'platform' else []
    if args.publish and any(r['tag_name'] == tag and not r['draft'] for r in catalog):
        parser.error('This version is already published; bump VERSION first')
    bundle = BASE / 'out/releases' / tag if args.skip_build else prepare(args, version, tag, commit, catalog)
    meta = verify(bundle, args, commit, tag)
    print('Prepared release: ' + str(bundle), flush=True)
    if args.publish:
        publish(args, bundle, meta, catalog)


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError) as exc:
        sys.exit(str(exc))
