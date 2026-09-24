"""Host tests of --taa-history-taps (X3M_TAA_HISTORY_TAPS, docs/architecture/taa-high-resolution.md S3): forwarded
only when given (the DLL default is 5), 5 or 16 only, TAA mode only, an inherited shell value never survives; the DLL
reads it and hands it to TemporalPass; the 16-tap twins keep the bytecode of the pre-S3 resolve programs (four since the
camera gate's dilated chain and its 16-tap twin were removed, 2026-09-24).
No game, no Wine."""
import json
from pathlib import Path
import tempfile
import unittest

import test_taa_sky_history as sky

ROOT, TAA = sky.ROOT, sky.TAA

# bytecode_sha256 of temporal-resolve{,-thin,-age,-far}-program.json before S3 (commit 483411b6); the -far-camera twin
# (0ef1f89547945ad6) went with the dilated camera chain (the camera gate's A' resolve has no 16-tap form).
PRE_S3 = {'': '507d843ed944c3cd', '-thin': '81e24fa941d57e7f', '-age': '97d65cd89577418d', '-far': 'd88d9dc355a82e93'}


class HistoryTapsLaunch(unittest.TestCase):
    # The launcher harness of the sky-history tests (dry run, never launches), without inheriting their tests.
    launch = sky.SkyHistoryLaunch.launch
    env = sky.SkyHistoryLaunch.env

    def test_omitted_is_not_forwarded_and_drops_an_inherited_value(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertNotIn('X3M_TAA_HISTORY_TAPS', self.env(directory, *TAA))
            self.assertNotIn('X3M_TAA_HISTORY_TAPS', self.env(directory, *TAA, inherited={'X3M_TAA_HISTORY_TAPS': '16'}))
            self.assertNotIn('X3M_TAA_HISTORY_TAPS', self.env(directory, '--motion-output', inherited={'X3M_TAA_HISTORY_TAPS': '16'}))

    def test_five_and_sixteen_are_forwarded(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('5', '16'):
                self.assertEqual(self.env(directory, *TAA, '--taa-history-taps', value)['X3M_TAA_HISTORY_TAPS'], value)

    def test_other_values_and_missing_taa_are_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('4', '9', '0', 'bilinear'):
                code, _, error = self.launch(directory, *TAA, '--taa-history-taps', value)
                self.assertNotEqual(code, 0, value)
                self.assertIn('--taa-history-taps', error)
            code, _, error = self.launch(directory, '--motion-output', '--taa-history-taps', '16')
            self.assertNotEqual(code, 0)
            self.assertIn('--taa-history-taps requires --taa', error)


class HistoryTapsSource(unittest.TestCase):
    def test_dll_reads_the_setting_and_configures_the_pass(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('GetEnvironmentVariableW(L"X3M_TAA_HISTORY_TAPS"', capture)
        self.assertIn('unsigned taa_history_taps = 5;', capture)
        # An oversized value (32+ characters, truncated by the read) logs and keeps 5, like X3M_TAA_MOTION_WEIGHT.
        self.assertIn('taa_history_taps_setting invalid=1 reason=too_long length=%lu', capture)
        self.assertIn('configure_history_taps(taa_history_taps)', capture)
        self.assertIn('taa_->configure_history_taps(taa_history_taps_)', (ROOT / 'src/proxy/motion_output.cpp').read_text())

    def test_sixteen_tap_twins_keep_the_pre_s3_bytecode(self):
        for suffix, prefix in PRE_S3.items():
            twin = json.loads((ROOT / f'verification/results/temporal-resolve{suffix}-taps16-program.json').read_text())
            default = json.loads((ROOT / f'verification/results/temporal-resolve{suffix}-program.json').read_text())
            self.assertTrue(twin['bytecode_sha256'].startswith(prefix), suffix)
            self.assertFalse(default['bytecode_sha256'].startswith(prefix), suffix)
            self.assertIn('src/temporal/resolve.hlsl', twin['includes'])


if __name__ == '__main__':
    unittest.main()
