"""Exercise target selection and dd handoff without opening any real device."""
import contextlib
import copy
import importlib.util
import io
from pathlib import Path
import stat
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('writer', Path(__file__).with_name('write-usb.py'))
writer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(writer)
DISK = {'path': '/dev/sdz', 'type': 'disk', 'tran': 'usb', 'size': 1000000,
        'model': 'Any USB stick', 'mountpoints': [None],
        'children': [{'mountpoints': [None]}]}


class WriterTests(unittest.TestCase):
    def test_accepts_generic_usb_including_nonremovable_ssd(self):
        writer.validate_target(DISK, 1000000)

    def test_rejects_unsafe_targets(self):
        for change in ({'type': 'part'}, {'tran': 'sata'}, {'size': 1},
                       {'mountpoints': ['/']},
                       {'children': [{'mountpoints': ['/media/usb']}]},
                       {'children': [{'mountpoints': ['[SWAP]']}]}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                writer.validate_target(dict(DISK, **change), 10)

    def test_lsblk_requests_partition_tree_and_filters_internal_disks(self):
        import json
        disks = [DISK, dict(DISK, type='part'), dict(DISK, tran='sata')]
        with patch.object(writer.subprocess, 'check_output', return_value=json.dumps({'blockdevices': disks})) as command:
            self.assertEqual(writer.usb_disks(), [DISK])
            self.assertIn('--tree', command.call_args.args[0])

    def run_writer(self, responses, changed_disk=None, dd_error=False):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / 'installer.img'
            image.write_bytes(b'test image')
            fake_stat = SimpleNamespace(st_mode=stat.S_IFBLK, st_rdev=123)
            with contextlib.ExitStack() as stack:
                stack.enter_context(patch('sys.argv', ['writer', str(image)]))
                stack.enter_context(patch('builtins.input', side_effect=responses))
                stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
                stack.enter_context(patch.object(writer.os, 'geteuid', return_value=0))
                stack.enter_context(patch.object(writer, 'usb_disks', side_effect=[[copy.deepcopy(DISK)], [changed_disk or DISK]]))
                # Only device stat is mocked; the image remains a real regular file.
                original_stat = Path.stat
                stack.enter_context(patch.object(Path, 'stat', lambda path, **kw: fake_stat if str(path) == '/dev/sdz' else original_stat(path, **kw)))
                opened = stack.enter_context(patch.object(writer.os, 'open', return_value=99))
                stack.enter_context(patch.object(writer.os, 'fstat', return_value=fake_stat))
                closed = stack.enter_context(patch.object(writer.os, 'close'))
                dd = stack.enter_context(patch.object(writer.subprocess, 'run'))
                if dd_error:
                    dd.side_effect = subprocess.CalledProcessError(1, 'dd')
                if changed_disk:
                    with self.assertRaises(ValueError):
                        writer.main()
                elif dd_error:
                    with self.assertRaises(subprocess.CalledProcessError):
                        writer.main()
                else:
                    writer.main()
                return opened, closed, dd

    def test_cancel_does_not_open_device(self):
        for responses in ([''], ['1', 'n']):
            with self.subTest(responses=responses):
                opened, _, dd = self.run_writer(responses)
                opened.assert_not_called()
                dd.assert_not_called()

    def test_mount_after_prompt_prevents_write(self):
        opened, _, dd = self.run_writer(['1', 'y'], dict(DISK, mountpoints=['/media/new']))
        opened.assert_not_called()
        dd.assert_not_called()

    def test_write_uses_exclusive_open_and_standard_dd(self):
        opened, closed, dd = self.run_writer(['1', 'y'])
        opened.assert_called_once_with(Path('/dev/sdz'), writer.os.O_WRONLY | writer.os.O_EXCL)
        self.assertEqual(dd.call_args.args[0], ['dd', 'bs=4M', 'status=progress', 'conv=fsync'])
        self.assertEqual(dd.call_args.kwargs['stdout'], 99)
        self.assertTrue(dd.call_args.kwargs['check'])
        closed.assert_called_once_with(99)

    def test_failed_write_closes_device(self):
        _, closed, _ = self.run_writer(['1', 'y'], dd_error=True)
        closed.assert_called_once_with(99)


if __name__ == '__main__':
    unittest.main()
