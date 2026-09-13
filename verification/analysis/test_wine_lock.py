"""Host-only lease/preflight controls: no Wine or fixture execution."""
import contextlib
import io
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'probe'))
import wine_lock


class PreflightTests(unittest.TestCase):
    def inventory(self, extra=''):
        return ('1 0 /sbin/launchd\n'
                '20 1 python3 verification/probe/run_motion_output.py\n'
                '30 20 python3 verification/probe/wine_lock.py python3 verification/probe/run_one.py\n'
                '40 1 python3 verification/probe/wine_lock.py --holder waiting wine fixture.exe\n' + extra)

    def check(self, extra=''):
        with patch.object(wine_lock.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, self.inventory(extra))):
            wine_lock.preflight(pid=30)

    def test_ancestors_and_waiting_wrappers_are_not_competitors(self):
        self.check()

    def test_actual_runner_fixture_and_game_are_competitors(self):
        for arguments in ('python3 -u verification/probe/run_d3d9_exports.py --dll /tmp/d3d9.dll',
                          'python3 verification/probe/compositor_bridge_run.py',
                          'python3 -- verification/probe/run_motion_output.py',
                          'python3 verification/probe/bloom_return_bridge_run.py',
                          'python3 verification/probe/check_bloom_shaders.py',
                          'python3 verification/probe/check_bloom_composition.py',
                          'python3 tools/shaders/generate_bloom_programs.py --check',
                          'C:\\test\\d3d9_exports_fixture.exe',
                          '/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine '
                          '--bottle X3 --no-update --dll d3d9=n,b --workdir /tmp /tmp/buffer_content.exe',
                          'wine-preloader /opt/wine/bin/wine64 /tmp/copied_depth_fixture.exe',
                          'C:\\X3\\X3AP.exe'):
            with self.subTest(arguments=arguments), self.assertRaisesRegex(RuntimeError, '50 '):
                self.check('50 1 ' + arguments + '\n')

    def test_mentions_are_not_execution(self):
        for arguments in ('/bin/sh -c python3 verification/probe/run_motion_output.py',
                          'rg run_d3d9_exports.py /tmp',
                          'python3 -c print("run_fixture.py")',
                          '/usr/bin/java ghidra.AnalyzeHeadless -process X3AP.exe',
                          'wineserver', 'C:\\windows\\system32\\winedevice.exe'):
            with self.subTest(arguments=arguments):
                self.check('50 1 ' + arguments + '\n')

    def test_unrelated_python_runners_do_not_block_wine(self):
        for arguments in ('python3 /opt/service/run_server.py',
                          'python3 tools/analysis/foo_run.py', 'python3 run_unrelated.py'):
            with self.subTest(arguments=arguments):
                self.check('50 1 ' + arguments + '\n')

    def test_inventory_errors_refuse(self):
        for text in ('', 'garbled', '1 0 /sbin/launchd\n'):
            with patch.object(wine_lock.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, text)):
                with self.assertRaises(RuntimeError):
                    wine_lock.preflight(pid=30)
        with patch.object(wine_lock.subprocess, 'run', side_effect=subprocess.TimeoutExpired('ps', 10)):
            with self.assertRaises(subprocess.TimeoutExpired):
                wine_lock.preflight(pid=30)


class LockCliTests(unittest.TestCase):
    def test_preflight_holds_lease_and_timing_records_child_status(self):
        with tempfile.TemporaryDirectory() as temporary:
            lock = Path(temporary) / 'lock'
            timing = Path(temporary) / 'timing.json'
            def check_held():
                with lock.open('r+') as other:
                    with self.assertRaises(BlockingIOError):
                        wine_lock.fcntl.flock(other, wine_lock.fcntl.LOCK_EX | wine_lock.fcntl.LOCK_NB)
            with patch.object(wine_lock, 'LOCK_PATH', str(lock)), \
                    patch.object(wine_lock, 'preflight', side_effect=check_held) as preflight, \
                    patch.object(wine_lock.subprocess, 'call', return_value=7) as child:
                self.assertEqual(wine_lock.main(['--timings-json', str(timing), '--holder', 'test', '--', 'host-command', '-q']), 7)
                preflight.assert_called_once_with()
                child.assert_called_once_with(['host-command', '-q'])
            data = json.loads(timing.read_text())
            self.assertEqual(data['exit_code'], 7)
            self.assertGreaterEqual(data['lock_wait_seconds'], 0)
            self.assertGreaterEqual(data['child_elapsed_seconds'], 0)
            self.assertEqual(lock.read_text(), '')

    def test_refusal_never_executes_child_and_releases_lease(self):
        with tempfile.TemporaryDirectory() as temporary:
            lock = Path(temporary) / 'lock'
            with patch.object(wine_lock, 'LOCK_PATH', str(lock)), \
                    patch.object(wine_lock, 'preflight', side_effect=RuntimeError('game running')), \
                    patch.object(wine_lock.subprocess, 'call') as child, contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(wine_lock.main(['host-command']), 75)
                child.assert_not_called()
            with lock.open('r+') as other:
                wine_lock.fcntl.flock(other, wine_lock.fcntl.LOCK_EX | wine_lock.fcntl.LOCK_NB)

    def test_optional_timing_write_error_preserves_child_status(self):
        with tempfile.TemporaryDirectory() as temporary:
            lock = Path(temporary) / 'lock'
            # A file cannot be the record's parent, independent of test-user ACLs.
            timing = lock / 'timing.json'
            with patch.object(wine_lock, 'LOCK_PATH', str(lock)), \
                    patch.object(wine_lock, 'preflight'), \
                    patch.object(wine_lock.subprocess, 'call', return_value=7), \
                    contextlib.redirect_stderr(io.StringIO()) as stderr:
                self.assertEqual(wine_lock.main(['--timings-json', str(timing), 'host-command']), 7)
            self.assertIn('could not write optional timings', stderr.getvalue())
            self.assertEqual(lock.read_text(), '')

    def test_no_command_is_cli_error(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
            wine_lock.main([])
        self.assertEqual(error.exception.code, 2)
