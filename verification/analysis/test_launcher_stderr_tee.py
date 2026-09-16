"""Host tests of the launcher's stderr/stdout tee in tools/manage.py: the UTC
line prefix, the terminal bytes kept unchanged, a fresh file per launch, the
refusal that costs the copy and not the launch, and the --dry-run path.
A fake child process; no game, no Wine."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import re
import sys
import tempfile
import threading
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
STAMP = re.compile(r'\[\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z\] ')


def load_manage():
    spec = importlib.util.spec_from_file_location('tee_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class LauncherTee(unittest.TestCase):
    def setUp(self):
        self.manage = load_manage()

    def test_stamp_is_utc_with_milliseconds(self):
        self.assertRegex(self.manage.utc_stamp() + ' ', STAMP)
        import datetime
        moment = datetime.datetime(2026, 9, 16, 7, 8, 9, 12345, tzinfo=datetime.timezone.utc)
        self.assertEqual(self.manage.utc_stamp(moment), '[2026-09-16T07:08:09.012Z]')

    def test_tee_keeps_the_terminal_bytes_and_stamps_the_copy(self):
        source = io.BytesIO(b'first\r\n\xff second\nno newline')
        terminal, log = io.BytesIO(), io.BytesIO()
        lines = self.manage.tee_stream(source, terminal, log, threading.Lock(),
                                       clock=lambda: '[2026-09-16T00:00:00.500Z]')
        self.assertEqual(lines, 3)
        self.assertEqual(terminal.getvalue(), b'first\r\n\xff second\nno newline')
        self.assertEqual(log.getvalue(),
                         b'[2026-09-16T00:00:00.500Z] first\n'
                         b'[2026-09-16T00:00:00.500Z] \xff second\n'
                         b'[2026-09-16T00:00:00.500Z] no newline\n')

    def child(self, directory, code=7, quiet=False):
        log = Path(directory) / 'captures' / 'launcher-stderr.log'
        program = 'import sys\n' if quiet else ('import sys\n'
                   'sys.stderr.write("(X3AP.exe:9) GStreamer-CRITICAL 10:20:30.123\\n")\n'
                   'sys.stderr.flush()\n'
                   'sys.stdout.write("out line\\n")\n')
        program += f'sys.exit({code})\n'
        out, err = io.BytesIO(), io.BytesIO()
        status = self.manage.launch_teed([sys.executable, '-c', program], None, directory, log,
                                         stdout=out, stderr=err)
        return status, log, out.getvalue(), err.getvalue()

    def test_child_streams_are_teed_and_the_file_starts_fresh(self):
        with tempfile.TemporaryDirectory() as directory:
            status, log, out, err = self.child(directory)
            self.assertEqual(status, 7)
            self.assertEqual(out, b'out line\n')
            self.assertIn(b'GStreamer-CRITICAL', err)
            written = log.read_text().splitlines()
            self.assertEqual(len(written), 2, written)
            for line in written:
                self.assertRegex(line + ' ', STAMP)
            self.assertEqual(sorted(STAMP.sub('', line) for line in written),
                             ['(X3AP.exe:9) GStreamer-CRITICAL 10:20:30.123', 'out line'])
            # A second launch replaces the file instead of appending to it.
            self.assertEqual(self.child(directory, code=0)[0], 0)
            self.assertEqual(len(log.read_text().splitlines()), 2)

    def test_an_unwritable_log_costs_the_copy_not_the_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            blocker = Path(directory) / 'captures'
            blocker.write_text('not a directory')
            error = io.StringIO()
            with contextlib.redirect_stderr(error):
                status, log, _, _ = self.child(directory, code=3, quiet=True)
            self.assertEqual(status, 3)
            self.assertFalse(log.exists())
            self.assertIn('not preserved', error.getvalue())

    def test_dry_run_prints_the_path_without_creating_it(self):
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory) / 'game'; game.mkdir()
            (game / 'X3AP.exe').touch()
            wine = Path(directory) / 'wine'; wine.touch()
            argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', str(game)]
            output = io.StringIO()
            with mock.patch.object(sys, 'argv', argv), mock.patch.object(self.manage, 'WINE', wine), \
                    mock.patch.object(self.manage.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                    contextlib.redirect_stdout(output):
                self.manage.main()
            expected = game.resolve() / 'x3-modern-captures' / 'launcher-stderr.log'
            self.assertEqual(json.loads(output.getvalue())['launcher_stderr'], str(expected))
            self.assertFalse(expected.exists())
            self.assertFalse(expected.parent.exists())


if __name__ == '__main__':
    unittest.main()
