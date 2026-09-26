"""Host-only lease/preflight controls: no Wine or fixture execution."""
import contextlib
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'probe'))
import fixture_process
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


BUILD = r'Z:\Users\dev\x3-mod\verification\probe\build'
ORPHAN = BUILD + r'\motion-output-seam-on-20260924\motion_output_fixture.exe Z:\tmp\a.fxo hook'


class OrphanTests(unittest.TestCase):
    """Orphaned crashed fixtures: reported by the preflight, ended only by --clean-orphans."""

    def rows(self, extra=None):
        rows = {1: (0, '/sbin/launchd'),
                30: (1, 'python3 verification/probe/wine_lock.py python3 verification/probe/run_one.py'),
                500: (1, ORPHAN),
                501: (500, r'C:\windows\system32\winedbg.exe --auto 212 8820'),
                600: (1, r'C:\X3\X3AP.exe'),
                601: (600, r'C:\windows\system32\winedbg.exe --auto 300 99'),
                700: (1, r'Z:\tmp\elsewhere\motion_output_fixture.exe'),
                701: (1, BUILD + r'\X3AP.exe'),
                702: (1, 'vim /Users/dev/x3-mod/verification/probe/build/case/motion_output_fixture.exe'),
                800: (1, r'C:\windows\system32\winedevice.exe')}
        rows.update(extra or {})
        return rows

    def test_orphan_and_its_debugger_are_identified(self):
        found = fixture_process.orphans(self.rows())
        self.assertEqual([(pid, debuggers) for pid, _, debuggers in found],
                         [(500, [(501, r'C:\windows\system32\winedbg.exe --auto 212 8820')])])

    def test_game_outside_build_and_mentions_never_match(self):
        rows = self.rows()
        for pid in (500, 501):
            del rows[pid]
        self.assertEqual(fixture_process.orphans(rows), [])
        self.assertEqual(fixture_process.unpaired_debuggers(rows, set()),
                         [(601, r'C:\windows\system32\winedbg.exe --auto 300 99')])

    def test_live_fixture_is_not_an_orphan(self):
        rows = self.rows({900: (20, BUILD + r'\motion-output-live\motion_output_fixture.exe'),
                          20: (1, 'python3 verification/probe/run_motion_output.py')})
        self.assertEqual([pid for pid, _, _ in fixture_process.orphans(rows)], [500])

    def test_preflight_names_the_orphan_and_refuses_without_killing(self):
        text = ''.join(f'{pid} {ppid} {args}\n' for pid, (ppid, args) in self.rows().items())
        with patch.object(wine_lock.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, text)), \
                patch.object(fixture_process.os, 'kill') as kill:
            with self.assertRaisesRegex(RuntimeError, r'(?s)600 .*PID 500 is an orphaned fixture.*winedbg --auto PID 501'
                                                      r'.*--clean-orphans.*kill -9 500 501'):
                wine_lock.preflight(pid=30)
            kill.assert_not_called()

    # Witness ps rows of the 2026-09-24 incident (CrossOver Preview): both PPID 1, a Y:\ worktree path, a bare winedbg.
    WITNESS_FIXTURE = (r'Y:\x3-mod\.claude\worktrees\...\verification\probe\build\motion-output-seam-ownership-'
                       r'bolt-shape-prims-.../motion_output_fixture.exe Z:')
    WITNESS_DEBUGGER = 'winedbg --auto 204 212'

    def witness(self, game=False):
        rows = {1: (0, '/sbin/launchd'),
                30: (1, 'python3 verification/probe/wine_lock.py python3 verification/probe/run_one.py'),
                51295: (1, self.WITNESS_FIXTURE), 51303: (1, self.WITNESS_DEBUGGER)}
        if game:
            rows[600] = (1, r'C:\X3\X3AP.exe')
        return rows

    def test_witness_y_drive_worktree_fixture_and_ppid1_debugger(self):
        rows = self.witness()
        self.assertEqual([(pid, debuggers) for pid, _, debuggers in fixture_process.orphans(rows)], [(51295, [])])
        self.assertEqual(fixture_process.orphan_debuggers(rows, {51295}), [(51303, self.WITNESS_DEBUGGER)])
        # With the game up a PPID-1 debugger may be the game's: not an orphan.
        self.assertEqual(fixture_process.orphan_debuggers(self.witness(game=True), {51295}), [])
        text = ''.join(f'{pid} {ppid} {args}\n' for pid, (ppid, args) in rows.items())
        with patch.object(wine_lock.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, text)), \
                patch.object(fixture_process.os, 'kill') as kill:
            with self.assertRaisesRegex(RuntimeError, r'(?s)PID 51295 is an orphaned fixture.*orphaned winedbg --auto '
                                                      r'\(PPID 1, no game running\): PID 51303.*kill -9 51295 51303'):
                wine_lock.preflight(pid=30)
            kill.assert_not_called()

    def test_witness_clean_orphans_ends_both_and_spares_debugger_while_game_runs(self):
        status, killed, child, text = self.clean(self.witness(), ['--clean-orphans'])
        self.assertEqual((status, sorted(killed)), (0, [51295, 51303]))
        self.assertIn('ending orphaned winedbg --auto PID 51303', text)
        status, killed, child, text = self.clean(self.witness(game=True), ['--clean-orphans'])
        self.assertEqual((status, sorted(killed)), (75, [51295]))
        self.assertIn('left winedbg --auto PID 51303', text)

    def clean(self, rows, argv):
        """Run main() under a fake table where killed PIDs disappear."""
        state = dict(rows)
        killed = []

        def kill(pid, signal):
            killed.append(pid)
            state.pop(pid, None)

        with tempfile.TemporaryDirectory() as temporary, \
                patch.object(wine_lock, 'LOCK_PATH', str(Path(temporary) / 'lock')), \
                patch.object(fixture_process, 'process_table', side_effect=lambda: dict(state)), \
                patch.object(fixture_process.os, 'kill', side_effect=kill), \
                patch.object(wine_lock.os, 'getpid', return_value=30), \
                patch.object(wine_lock.subprocess, 'call', return_value=0) as child, \
                contextlib.redirect_stderr(io.StringIO()) as stderr:
            status = wine_lock.main(argv)
        return status, killed, child, stderr.getvalue()

    def test_clean_orphans_ends_exactly_the_orphan_pair(self):
        # 600/601 (game) and 700/701 (a fixture outside the build tree, a game-named image) would keep the refusal.
        rows = {pid: row for pid, row in self.rows().items() if pid not in (600, 601, 700, 701)}
        status, killed, child, text = self.clean(rows, ['--clean-orphans'])
        self.assertEqual((status, sorted(killed)), (0, [500, 501]))
        child.assert_not_called()
        self.assertIn('ending orphaned fixture PID 500', text)
        # With the game and a fixture outside the build tree up, only the orphan pair is
        # ended; the game, its debugger and the other fixture are left and the preflight refuses.
        status, killed, child, text = self.clean(self.rows(), ['--clean-orphans', 'host-command'])
        self.assertEqual((status, sorted(killed)), (75, [500, 501]))
        child.assert_not_called()
        self.assertIn('left winedbg --auto PID 601', text)
        self.assertRegex(text, r'preflight refused: .*600 .*700 ')

    def test_clean_orphans_refuses_live_fixture_without_killing(self):
        rows = {pid: row for pid, row in self.rows().items() if pid not in (500, 501, 600, 601)}
        rows.update({20: (1, 'python3 verification/probe/run_motion_output.py'),
                     900: (20, BUILD + r'\motion-output-live\motion_output_fixture.exe')})
        status, killed, child, text = self.clean(rows, ['--clean-orphans', 'host-command'])
        self.assertEqual((status, killed), (75, []))
        child.assert_not_called()
        self.assertIn('no orphaned fixture found', text)
        self.assertIn('preflight refused', text)

    def test_survivor_keeps_the_refusal(self):
        rows = {pid: row for pid, row in self.rows().items() if pid not in (600, 601, 700, 701)}
        with tempfile.TemporaryDirectory() as temporary, \
                patch.object(wine_lock, 'LOCK_PATH', str(Path(temporary) / 'lock')), \
                patch.object(fixture_process, 'process_table', return_value=rows), \
                patch.object(fixture_process.os, 'kill'), patch.object(fixture_process, 'WAIT_SECONDS', 0.3), \
                patch.object(wine_lock.subprocess, 'call') as child, contextlib.redirect_stderr(io.StringIO()) as stderr:
            self.assertEqual(wine_lock.main(['--clean-orphans', 'host-command']), 75)
        child.assert_not_called()
        self.assertIn('still alive after SIGTERM, 0 s and SIGKILL: 500 501', stderr.getvalue())

    def test_native_commands_naming_a_fixture_are_never_orphans(self):
        rows = {1: (0, '/sbin/launchd')}
        for pid, arguments in enumerate(('/usr/bin/less ./verification/probe/build/case/motion_output_fixture.exe',
                                         '/usr/bin/tail -f ./verification/probe/build/case/x.exe',
                                         '/usr/bin/python3 -c print ./verification/probe/build/c/x.exe',
                                         '/Users/dev/x3-mod/verification/probe/build/case/motion_output_fixture.exe'),
                                        start=100):
            rows[pid] = (1, arguments)
        self.assertEqual(fixture_process.orphans(rows), [])
        self.assertEqual(fixture_process.image(rows[100][1]), '/usr/bin/less')
        self.assertEqual(fixture_process.image(rows[101][1]), '/usr/bin/tail')

    def test_end_refuses_protected_pids_before_any_signal(self):
        rows = {1: (0, '/sbin/launchd'), 45: (1, 'zsh'), 40: (45, 'login'), 30: (40, 'python3 wine_lock.py'),
                500: (1, ORPHAN)}
        state = dict(rows)
        signals = []
        with patch.object(fixture_process.os, 'getpid', return_value=30), \
                patch.object(fixture_process.os, 'getppid', return_value=40), \
                patch.object(fixture_process.os, 'kill', side_effect=lambda pid, number: (signals.append((pid, number)),
                                                                                           state.pop(pid, None))):
            survivors, refused = fixture_process.end({0, 1, 30, 40, 45, 500}, rows, lambda: dict(state), 1)
        self.assertEqual((survivors, refused), (set(), {0, 1, 30, 40, 45}))
        self.assertEqual(signals, [(500, fixture_process.signal.SIGTERM)])

    def test_sigterm_then_sigkill(self):
        rows = {1: (0, '/sbin/launchd'), 500: (1, ORPHAN)}
        signals = []
        with patch.object(fixture_process.os, 'kill', side_effect=lambda pid, number: signals.append(number)):
            survivors, refused = fixture_process.end({500}, rows, lambda: dict(rows), 0.2)
        self.assertEqual((survivors, refused), ({500}, set()))
        self.assertEqual(signals, [fixture_process.signal.SIGTERM, fixture_process.signal.SIGKILL])


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
                child.assert_called_once_with(['host-command', '-q'], env=mock.ANY)
                self.assertEqual(child.call_args.kwargs['env']['X3M_CONFIG'], os.environ.get('X3M_CONFIG', 'bare'))
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

    def test_child_environment_is_bare_unless_set(self):
        # The settings file (docs/architecture/config-file.md): fixtures run the proxy without defaults or x3m.ini.
        self.assertEqual(wine_lock.child_environment({'PATH': '/bin'}), {'PATH': '/bin', 'X3M_CONFIG': 'bare'})
        self.assertEqual(wine_lock.child_environment({'X3M_CONFIG': 'none'})['X3M_CONFIG'], 'none')
