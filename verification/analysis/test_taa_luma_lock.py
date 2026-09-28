"""Host tests of --taa-luma-lock (X3M_TAA_LUMA_LOCK, docs/architecture/taa-luminance-lock.md, opt-in): absent = off and
never sent, forwarded as the full triple when given, requires --taa with the far stabiliser weight and the thin region;
the schema entry, the DLL's read site and the resolve's register block match. No game, no Wine."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
from source_text import source_text

ROOT = Path(__file__).resolve().parents[2]
TAA = ['--motion-output', '--ownership', '--object-trace', '--object-lifetime', '--taa']
NAME = 'X3M_TAA_LUMA_LOCK'


def load_manage():
    if str(ROOT / 'tools') not in sys.path:
        sys.path.insert(0, str(ROOT / 'tools'))  # manage.py imports its sibling modules (media_package) by name
    spec = importlib.util.spec_from_file_location('luma_lock_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class LumaLockLaunch(unittest.TestCase):
    def launch(self, directory, *args, inherited=None):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', str(game), *args]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), mock.patch.object(module, 'VOICE_DECODER_REPO', None), \
                mock.patch.dict(module.os.environ, inherited or {}), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, output.getvalue(), error.getvalue()
        return 0, output.getvalue(), error.getvalue()

    def env(self, directory, *args, inherited=None):
        code, output, error = self.launch(directory, *args, inherited=inherited)
        self.assertEqual(code, 0, error)
        return json.loads(output)['env']

    def test_opt_in_forwarding(self):
        far = ('--taa-far-stabiliser', '0.985', '--taa-thin-region', '0.97')
        with tempfile.TemporaryDirectory() as directory:
            self.assertNotIn(NAME, self.env(directory, *TAA, *far))
            self.assertNotIn(NAME, self.env(directory, *TAA, *far, inherited={NAME: '16'}))  # a stale shell value is dropped
            self.assertNotIn(NAME, self.env(directory, inherited={NAME: '16'}))
            self.assertEqual(self.env(directory, *TAA, *far, '--taa-luma-lock', '16')[NAME], '16,0.25,3')
            self.assertEqual(self.env(directory, *TAA, *far, '--taa-luma-lock', '31,0.5,4')[NAME], '31,0.5,4')
            self.assertEqual(self.env(directory, *TAA, *far, '--taa-luma-lock', '1,0,0')[NAME], '1,0,0')
            self.assertEqual(self.env(directory, *TAA, '--taa-luma-lock', '0')[NAME], '0,0.25,3')  # explicit off
            for value in ('32', '64', '65', '-1', '16.5', 'nan', 'x', '16,1.5,2', '16,0.25,33', '16,-0.1,2', '16,0.25', '16,0.25,2,1', ''):
                code, _, error = self.launch(directory, *TAA, *far, '--taa-luma-lock', value)
                self.assertNotEqual(code, 0, value)
                self.assertIn('--taa-luma-lock', error)
            code, _, error = self.launch(directory, *TAA, *far, '--taa-luma-lock', '32,0.25,3')  # the lane holds T in 5 bits
            self.assertNotEqual(code, 0)
            self.assertIn('T is at most 31 frames', error)
            code, _, error = self.launch(directory, '--motion-output', '--taa-luma-lock', '16')
            self.assertNotEqual(code, 0)
            self.assertIn('--taa-luma-lock requires --taa', error)
            for args in (('--taa-far-stabiliser', '0', '--taa-thin-region', '0.97'), ('--taa-far-stabiliser', '0.985', '--taa-thin-region', '0'), ()):
                code, _, error = self.launch(directory, *TAA, *args, '--taa-luma-lock', '16')
                self.assertNotEqual(code, 0, args)
                self.assertIn('--taa-luma-lock requires the far stabiliser weight and the thin region', error)

    def test_schema_site_and_registers(self):
        sys.path.insert(0, str(ROOT / 'tools/config'))
        import schema  # noqa: E402
        entry = schema.BY_KEY['taa_luma_lock']
        self.assertEqual((entry['type'], entry['default'], entry['builtin'], entry['counts'], entry['launcher'], entry['developer']),
                         ('float_list', None, '0', (1, 3), '--taa-luma-lock', False))
        self.assertEqual(entry['requires'], ('taa', 'taa_far_stabiliser', 'taa_thin_region'))
        output = source_text(ROOT / 'src/proxy/motion_output.cpp')
        for reason in ('"thin_region_off"', '"camera_gate"', '"far_off"', '"render_targets"'):
            self.assertIn(reason, output)
        self.assertIn('Needs: taa, taa_far_stabiliser, taa_thin_region.', (ROOT / 'assets/x3m.ini').read_text())
        self.assertTrue(schema.BY_KEY['taa_luma_lock_release']['developer'] and schema.BY_KEY['taa_luma_lock_gate']['developer'])
        self.assertEqual(schema.BY_KEY['taa_luma_lock_gate']['choices'], ('camera', 'screen', 'always'))
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        for name in ('L"X3M_TAA_LUMA_LOCK"', 'L"X3M_TAA_LUMA_LOCK_RELEASE"', 'L"X3M_TAA_LUMA_LOCK_GATE"', 'luma_lock=%u,%g,%g'):
            self.assertIn(name, capture)
        resolve = source_text(ROOT / 'src/temporal/resolve.h')
        self.assertIn('kLumaLockRegister = 14, kLumaLockFramesMax = 31', resolve)
        shader = (ROOT / 'src/temporal/resolve.hlsl').read_text()
        self.assertIn('float4 lockParams : register(c14);', shader)
        self.assertIn('float4 lockGate : register(c15);', shader)
        self.assertIn('sampler2D previousLock : register(s13);', shader)


if __name__ == '__main__':
    unittest.main()
