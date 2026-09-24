"""Host tests of the window/cursor launcher options in tools/manage.py
(--window-monitor-rect / --no-window-monitor-rect, --window-trace,
--cursor-reassert): the default with its _DEFAULT marker on a modded launch,
the opt-outs, nothing under --vanilla, stale shell values, the refusals and an
unchanged command line (--dry-run only, never a launch). No game, no Wine."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
NAMES = ('X3M_WINDOW_MONITOR_RECT', 'X3M_WINDOW_MONITOR_RECT_DEFAULT', 'X3M_WINDOW_TRACE', 'X3M_CURSOR_REASSERT')
STALE = {'X3M_WINDOW_MONITOR_RECT': '1', 'X3M_WINDOW_MONITOR_RECT_DEFAULT': '1', 'X3M_WINDOW_TRACE': '1', 'X3M_CURSOR_REASSERT': '1'}


def load_manage():
    spec = importlib.util.spec_from_file_location('window_options_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class WindowLaunchOptions(unittest.TestCase):
    def launch(self, directory, *args, inherited=None, vanilla=False):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        if not vanilla:   # a modded launch wants an installed proxy that matches its manifest
            (game / 'd3d9.dll').write_bytes(b'proxy')
            (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', *(['--vanilla'] if vanilla else []), '--game-dir', str(game), *args]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), \
                mock.patch.dict(module.os.environ, inherited or {}), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, output.getvalue(), error.getvalue()
        return 0, output.getvalue(), error.getvalue()

    def window_env(self, directory, *args, **kwargs):
        code, output, error = self.launch(directory, *args, **kwargs)
        self.assertEqual(code, 0, error)
        return {k: v for k, v in json.loads(output)['env'].items() if k in NAMES}

    def test_default_opt_out_and_vanilla(self):
        with tempfile.TemporaryDirectory() as directory:
            default = {'X3M_WINDOW_MONITOR_RECT': '1', 'X3M_WINDOW_MONITOR_RECT_DEFAULT': '1'}
            explicit = {'X3M_WINDOW_MONITOR_RECT': '1', 'X3M_WINDOW_MONITOR_RECT_DEFAULT': '0'}
            off = {'X3M_WINDOW_MONITOR_RECT': '0', 'X3M_WINDOW_MONITOR_RECT_DEFAULT': '0'}
            self.assertEqual(self.window_env(directory), default)
            self.assertEqual(self.window_env(directory, inherited=STALE), default)   # stale trace/reassert values never travel
            self.assertEqual(self.window_env(directory, '--window-monitor-rect', 'on'), explicit)
            self.assertEqual(self.window_env(directory, '--window-monitor-rect', 'off', inherited=STALE), off)
            self.assertEqual(self.window_env(directory, '--no-window-monitor-rect', inherited=STALE), off)
            self.assertEqual(self.window_env(directory, vanilla=True, inherited=STALE), {})
            self.assertEqual(self.window_env(directory, '--no-window-monitor-rect', vanilla=True, inherited=STALE), {})

    def test_trace_and_reassert(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(self.window_env(directory, '--telemetry', '--window-trace', '--cursor-reassert'),
                             {'X3M_WINDOW_MONITOR_RECT': '1', 'X3M_WINDOW_MONITOR_RECT_DEFAULT': '1', 'X3M_WINDOW_TRACE': '1', 'X3M_CURSOR_REASSERT': '1'})
            self.assertEqual(self.window_env(directory, '--cursor-reassert', '--no-window-monitor-rect'),
                             {'X3M_WINDOW_MONITOR_RECT': '0', 'X3M_WINDOW_MONITOR_RECT_DEFAULT': '0', 'X3M_CURSOR_REASSERT': '1'})
            traced = json.loads(self.launch(directory, '--telemetry', '--window-trace')[1])['env']
            self.assertEqual(traced['X3M_TELEMETRY'], '1')
            self.assertNotIn('X3M_CURSOR_REASSERT', traced)

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as directory:
            for args, vanilla, message in ((('--window-trace',), False, '--window-trace requires --telemetry'),
                                           (('--window-monitor-rect',), False, 'expected one argument'),
                                           (('--window-monitor-rect', 'on'), True, 'cannot be combined with --vanilla'),
                                           (('--telemetry', '--window-trace'), True, 'cannot be combined with --vanilla'),
                                           (('--cursor-reassert',), True, 'cannot be combined with --vanilla'),
                                           (('--window-monitor-rect', 'maybe'), False, 'invalid choice')):
                with self.subTest(args=args, vanilla=vanilla):
                    code, _, error = self.launch(directory, *args, vanilla=vanilla)
                    self.assertNotEqual(code, 0)
                    self.assertIn(message, error)

    def test_option_never_consumes_the_action(self):
        # Before the option was placed ahead of the action: with an optional value it could swallow
        # 'launch'. It now requires on|off, so 'launch' is refused as a value and the explicit form parses.
        with tempfile.TemporaryDirectory() as directory:
            module = load_manage()
            game = Path(directory) / 'game'
            game.mkdir()
            (game / 'X3AP.exe').touch()
            (game / 'd3d9.dll').write_bytes(b'proxy')
            (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
            wine = Path(directory) / 'wine'
            wine.touch()
            for argv, expect in ((['manage.py', '--window-monitor-rect', 'launch', '--dry-run', '--game-dir', str(game)], None),
                                 (['manage.py', '--window-monitor-rect', 'off', 'launch', '--dry-run', '--game-dir', str(game)], '0'),
                                 (['manage.py', '--no-window-monitor-rect', 'launch', '--dry-run', '--game-dir', str(game)], '0'),
                                 (['manage.py', 'launch', '--dry-run', '--game-dir', str(game)], '1')):
                with self.subTest(argv=argv[1:3]):
                    output, error = io.StringIO(), io.StringIO()
                    with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), \
                            mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                            contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
                        try:
                            module.main()
                            code = 0
                        except SystemExit as exit_error:
                            code = exit_error.code
                    if expect is None:
                        self.assertEqual(code, 2)
                        self.assertIn("invalid choice: 'launch'", error.getvalue())
                    else:
                        self.assertEqual(code, 0, error.getvalue())
                        self.assertEqual(json.loads(output.getvalue())['env']['X3M_WINDOW_MONITOR_RECT'], expect)

    def test_launch_command_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            on = json.loads(self.launch(directory)[1])
            both = json.loads(self.launch(directory, '--cursor-reassert', '--no-window-monitor-rect')[1])
            self.assertEqual(on['command'], both['command'])
            self.assertEqual({k: v for k, v in both['env'].items() if on['env'].get(k) != v},
                             {'X3M_WINDOW_MONITOR_RECT': '0', 'X3M_WINDOW_MONITOR_RECT_DEFAULT': '0', 'X3M_CURSOR_REASSERT': '1'})


if __name__ == '__main__':
    unittest.main()
