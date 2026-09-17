"""Host tests of the busy-frame environment experiments in tools/manage.py
(docs/architecture/effect-pass-replay.md, "Environment experiments"): unset
leaves the child environment alone, --fex-tso and --wined3d each set exactly
their one variable, an ill-formed --wined3d string is refused, the --dry-run
JSON reports both, and the launcher header records them. No game, no Wine."""
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


def load_manage():
    spec = importlib.util.spec_from_file_location('env_experiment_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def fake_game(directory):
    game = Path(directory) / 'game'
    game.mkdir(exist_ok=True)
    (game / 'X3AP.exe').touch()
    dll = game / 'd3d9.dll'
    dll.write_bytes(b'proxy')
    (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
    return game


class EnvExperimentOptions(unittest.TestCase):
    def launch(self, directory, *args):
        module = load_manage()
        game = fake_game(directory)
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', '--game-dir', str(game), *args]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, output.getvalue(), error.getvalue()
        return 0, output.getvalue(), error.getvalue()

    def child_env(self, directory, *args):
        """The environment the child would receive, captured from a real
        (mocked) launch rather than from the dry-run summary."""
        module = load_manage()
        game = fake_game(directory)
        wine = Path(directory) / 'wine'
        wine.touch()
        seen = {}

        def fake_launch(command, env, cwd, log_path, **kwargs):
            seen['env'], seen['header'] = env, kwargs.get('header')
            return 0

        argv = ['manage.py', 'launch', '--game-dir', str(game), *args]
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), \
                mock.patch.object(module, 'launch_teed', fake_launch), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                module.main()
        return seen

    def test_unset_leaves_both_variables_absent(self):
        with tempfile.TemporaryDirectory() as directory:
            # Even a stale shell value must not enter the experiment.
            with mock.patch.dict('os.environ', {'FEX_TSOENABLED': '0', 'WINE_D3D_CONFIG': 'csmt=0x0'}):
                seen = self.child_env(directory)
            self.assertNotIn('FEX_TSOENABLED', seen['env'])
            self.assertNotIn('WINE_D3D_CONFIG', seen['env'])
            self.assertIn(' fex_tso=unset wined3d=unset', seen['header'])
            self.assertEqual(json.loads(self.launch(directory)[1])['env'].keys() & {'FEX_TSOENABLED', 'WINE_D3D_CONFIG'}, set())

    def test_fex_tso_sets_the_fex_variable_only(self):
        with tempfile.TemporaryDirectory() as directory:
            off = self.child_env(directory, '--fex-tso', 'off')
            self.assertEqual(off['env']['FEX_TSOENABLED'], '0')
            self.assertNotIn('WINE_D3D_CONFIG', off['env'])
            self.assertIn(' fex_tso=off wined3d=unset', off['header'])
            self.assertEqual(self.child_env(directory, '--fex-tso', 'on')['env']['FEX_TSOENABLED'], '1')

    def test_wined3d_sets_the_config_variable_only(self):
        with tempfile.TemporaryDirectory() as directory:
            seen = self.child_env(directory, '--wined3d', 'csmt=0x0;renderer=vulkan')
            self.assertEqual(seen['env']['WINE_D3D_CONFIG'], 'csmt=0x0;renderer=vulkan')
            self.assertNotIn('FEX_TSOENABLED', seen['env'])
            self.assertIn(' fex_tso=unset wined3d=csmt=0x0;renderer=vulkan', seen['header'])

    def test_malformed_wined3d_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            for bad in ('csmt', 'csmt=0x0;', 'csmt=0x0;bad!', 'csmt = 0x0', ''):
                code, _, error = self.launch(directory, '--wined3d', bad)
                self.assertEqual(code, 2, bad)
                self.assertIn('--wined3d', error)

    def test_dry_run_json_reports_both(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, error = self.launch(directory, '--fex-tso', 'off', '--wined3d', 'csmt=0x0')
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual(env['FEX_TSOENABLED'], '0')
            self.assertEqual(env['WINE_D3D_CONFIG'], 'csmt=0x0')
            # The command itself is unchanged: this is an environment-only experiment.
            self.assertEqual(json.loads(output)['command'], json.loads(self.launch(directory)[1])['command'])

    def test_options_are_launch_only(self):
        module = load_manage()
        for args in (['--fex-tso', 'off'], ['--wined3d', 'csmt=0x0']):
            with tempfile.TemporaryDirectory() as directory:
                argv = ['manage.py', 'status', '--game-dir', str(fake_game(directory)), *args]
                with mock.patch.object(sys, 'argv', argv), \
                        contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()) as error:
                    with self.assertRaises(SystemExit) as exit_error:
                        module.main()
                self.assertEqual(exit_error.exception.code, 2)
                self.assertIn('launch only', error.getvalue())


if __name__ == '__main__':
    unittest.main()
