"""Host tests of --taa-region-hold (X3M_TAA_REGION_HOLD, docs/architecture/taa-plan-lifted-slot-cap.md step 1): forwarded
only when given (the DLL default is on), on or off only, TAA mode only, an inherited shell value never survives; the DLL
reads it (bounded, too_long logged) and hands it to TemporalPass; the hold-off programs keep their bytecode (the box
programs of ee3bbf88; the 16-tap twins of the pre-S3 resolve). No game, no Wine."""
import json
import tempfile
import unittest

import test_taa_sky_history as sky
from test_taa_history_taps import PRE_S3

ROOT, TAA = sky.ROOT, sky.TAA

# bytecode_sha256 of the hold-off box programs at ee3bbf88 (S3): the hold twins are separate programs, these stay as they were.
BOXES = {'temporal-thin-box': 'e2c8074379ca2b9e', 'temporal-thin-box-rows': '24e69fac7877ae49', 'temporal-thin-box-columns': '0667b9f106e6b016'}


class RegionHoldLaunch(unittest.TestCase):
    launch = sky.SkyHistoryLaunch.launch
    env = sky.SkyHistoryLaunch.env

    def test_omitted_is_not_forwarded_and_drops_an_inherited_value(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertNotIn('X3M_TAA_REGION_HOLD', self.env(directory, *TAA))
            self.assertNotIn('X3M_TAA_REGION_HOLD', self.env(directory, *TAA, inherited={'X3M_TAA_REGION_HOLD': 'off'}))
            self.assertNotIn('X3M_TAA_REGION_HOLD', self.env(directory, '--motion-output', inherited={'X3M_TAA_REGION_HOLD': 'off'}))

    def test_on_and_off_are_forwarded(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('on', 'off'):
                self.assertEqual(self.env(directory, *TAA, '--taa-region-hold', value)['X3M_TAA_REGION_HOLD'], value)

    def test_other_values_and_missing_taa_are_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('1', '0', 'yes', 'hold'):
                code, _, error = self.launch(directory, *TAA, '--taa-region-hold', value)
                self.assertNotEqual(code, 0, value)
                self.assertIn('--taa-region-hold', error)
            code, _, error = self.launch(directory, '--motion-output', '--taa-region-hold', 'off')
            self.assertNotEqual(code, 0)
            self.assertIn('--taa-region-hold requires --taa', error)


class RegionHoldSource(unittest.TestCase):
    def test_dll_reads_the_setting_and_configures_the_pass(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('GetEnvironmentVariableW(L"X3M_TAA_REGION_HOLD",setting,32)', capture)
        self.assertIn('bool taa_region_hold = true;', capture)
        self.assertIn('taa_region_hold_setting invalid=1 reason=too_long length=%lu', capture)
        self.assertIn('configure_region_hold(taa_region_hold)', capture)
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        self.assertIn('taa_->configure_region_hold()', motion)
        self.assertIn('in.thin_region_hold = taa_region_hold_;', motion)
        self.assertIn('ps30_slots=%u', motion)

    def test_hold_programs_are_recorded_and_hold_off_programs_unchanged(self):
        for name in ('temporal-resolve-far-camera-hold', 'temporal-thin-box-hold', 'temporal-thin-box-rows-hold', 'temporal-thin-box-columns-hold'):
            record = json.loads((ROOT / f'verification/results/{name}-program.json').read_text())
            self.assertEqual(record['target'], 'ps_3_0', name)
        for name, prefix in BOXES.items():
            self.assertTrue(json.loads((ROOT / f'verification/results/{name}-program.json').read_text())['bytecode_sha256'].startswith(prefix), name)
        for suffix, prefix in PRE_S3.items():
            self.assertTrue(json.loads((ROOT / f'verification/results/temporal-resolve{suffix}-taps16-program.json').read_text())['bytecode_sha256'].startswith(prefix), suffix)


if __name__ == '__main__':
    unittest.main()
