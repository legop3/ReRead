"""Focused release-policy tests; no GitHub writes or Docker builds."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
from datetime import datetime, timezone
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'repository'))
from install_page import latest_installer, build
sys.path.insert(0, str(ROOT / 'antix'))
from apps.package import build as build_app
spec = importlib.util.spec_from_file_location('releasing', ROOT / 'release.py')
releasing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(releasing)


def release(tag, date, files, **extra):
    return dict(tag_name=tag, published_at=date, draft=False, prerelease=False,
                html_url='https://example.test/' + tag,
                assets=[dict(name=name, size=100, browser_download_url='https://example.test/' + tag + '/' + name)
                        for name in files], **extra)


class ReleasePolicy(unittest.TestCase):
    def test_installer_selection_ignores_apps_drafts_and_partial_uploads(self):
        files = ['reread-installer-usb.img', 'reread-installer-usb.img.sha256']
        old = release('v1', '2026-01-01T00:00:00Z', files)
        current = release('v2', '2026-02-01T00:00:00Z', files)
        app = release('app-v9', '2026-03-01T00:00:00Z', ['app.deb'])
        partial = release('v3', '2026-04-01T00:00:00Z', files[:1])
        draft = release('v4', '2026-05-01T00:00:00Z', files);draft['draft'] = True
        preview = release('v5', '2026-06-01T00:00:00Z', files);preview['prerelease'] = True
        catalog = [app, preview, old, partial, draft, current]
        self.assertEqual(latest_installer(catalog)['tag'], 'v2')
        with tempfile.TemporaryDirectory() as temporary:
            build(catalog, temporary)
            page = Path(temporary, 'install/index.html').read_text()
            self.assertIn('https://example.test/v2/reread-installer-usb.img', page)
            self.assertEqual(json.loads(Path(temporary, 'install/latest.json').read_text())['tag'], 'v2')

    def test_no_completed_installer_is_an_error(self):
        with self.assertRaises(ValueError):latest_installer([])

    def test_prepared_bytes_and_source_must_match(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle = Path(temporary)
            package = bundle / 'app.deb';package.write_bytes(b'package')
            metadata = dict(commit='abc', tag='app-v1', repository='owner/repo', assets={'app.deb': releasing.sha(package)})
            (bundle / 'release.json').write_text(json.dumps(metadata))
            assets = dict(metadata['assets'], **{'release.json': releasing.sha(bundle / 'release.json')})
            (bundle / 'SHA256SUMS').write_text(''.join(f'{digest}  {name}\n' for name,digest in sorted(assets.items())))
            args = SimpleNamespace(repo='owner/repo')
            releasing.verify(bundle, args, 'abc', 'app-v1')
            with patch.object(releasing, 'same_build_source', return_value=False):
                with self.assertRaises(RuntimeError):releasing.verify(bundle, args, 'other', 'app-v1')
            package.write_bytes(b'changed')
            with self.assertRaises(RuntimeError):releasing.verify(bundle, args, 'abc', 'app-v1')

    def test_conflicting_published_package_is_rejected_before_push(self):
        catalog = [release('old', '2026-01-01T00:00:00Z', ['app.deb'])]
        catalog[0]['assets'][0]['digest'] = 'sha256:old'
        metadata = dict(tag='app-v2', commit='abc', assets={'app.deb': 'new'})
        with patch.object(releasing, 'run') as command:
            with self.assertRaisesRegex(RuntimeError, 'different bytes'):
                releasing.publish(SimpleNamespace(repo='owner/repo'), Path('/unused'), metadata, catalog)
            command.assert_not_called()

    def test_app_recipe_preserves_custom_output_and_all_architecture(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            source = base / 'antix/apps/example';source.mkdir(parents=True)
            (source / 'VERSION').write_text('1.0')
            sdk = base / 'build/ui';sdk.mkdir(parents=True)
            (sdk / 'libreader-ui.so').touch()
            (source / 'package.py').write_text("def build(base, build_dir, output):\n    (output / 'reread-app-example_1.0_all.deb').write_bytes(b'package')\n")
            with patch('apps.package.subprocess.run'):
                package = build_app('example', base, base / 'build', base / 'custom-output')
            self.assertEqual(package.parent, base / 'custom-output')
            self.assertEqual(package.read_bytes(), b'package')

    def test_app_publication_keeps_latest_for_platform_and_waits_for_its_tag(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle = Path(temporary)
            for name in ('app.deb', 'release.json', 'SHA256SUMS'):(bundle / name).write_text(name)
            tag = 'images-v1'
            draft = dict(tag_name=tag, draft=True, prerelease=False, target_commitish='abc',
                         assets=[dict(name=name, digest='sha256:' + releasing.sha(bundle / name))
                                 for name in ('app.deb', 'release.json', 'SHA256SUMS')])
            args = SimpleNamespace(repo='owner/repo', kind='app', app='images', notes_file=None)
            metadata = dict(tag=tag, commit='abc', assets={'app.deb': releasing.sha(bundle / 'app.deb')})
            now = datetime.now(timezone.utc).isoformat()
            jobs = [dict(databaseId=99, event='release', headBranch='another-app', createdAt=now),
                    dict(databaseId=100, event='release', headBranch=tag, createdAt=now)]
            def command(*arguments, **kwargs):
                if arguments[:3] == ('git', 'rev-parse', 'HEAD'):return 'abc'
                return ''
            with patch.object(releasing, 'run', side_effect=command) as commands, \
                 patch.object(releasing, 'release_detail', return_value=draft), \
                 patch.object(releasing, 'gh_json', return_value=jobs):
                releasing.publish(args, bundle, metadata, [draft])
            calls = [call.args for call in commands.call_args_list]
            edit = next(call for call in calls if call[:3] == ('gh', 'release', 'edit'))
            self.assertIn('--latest=false', edit)
            self.assertTrue(any(call[:4] == ('gh', 'run', 'watch', 100) for call in calls))

    def test_draft_lookup_uses_release_id_and_retries_visibility_delay(self):
        import subprocess
        missing = subprocess.CalledProcessError(1, ['gh'])
        detail = dict(tag_name='j2me-v1', assets=[])
        with patch.object(releasing, 'gh_json', side_effect=[missing, {'databaseId': 123}, detail]) as api, \
             patch.object(releasing.time, 'sleep'):
            self.assertEqual(releasing.release_detail('owner/repo', 'j2me-v1', required=True), detail)
        self.assertIn('/releases/123?', api.call_args.args[1])

    def test_publisher_fix_can_resume_but_changed_build_inputs_cannot(self):
        for changed, permitted in [('release.py\nrepository/test_releases.py', True),
                                    ('TODO.md', True),
                                    ('antix/apps/j2me/main.cpp', False),
                                    ('antix/apps/package.py', False)]:
            with self.subTest(changed=changed), patch.object(releasing, 'run', side_effect=[None, changed]):
                self.assertEqual(releasing.same_build_source('built', 'current'), permitted)


if __name__ == '__main__':unittest.main()
