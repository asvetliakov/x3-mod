"""Host tests of the --taa-sentinel-stabiliser launcher default (X3M_TAA_SENTINEL_STABILISER, docs/architecture/
fade-rt2-ownership.md section 5): off (0) on every modded --taa launch since 2026-09-25 with
X3M_TAA_SENTINEL_STABILISER_DEFAULT=1; an explicit value (0.7 restores the previous look, 0 or off the same as the default)
sends marker 0; nothing without --taa or under --vanilla, and an inherited shell value or marker never survives. The DLL's
fallback when the variable is unset stays 0.7 under the camera gate (fixtures unchanged); it reads the marker only with a
valid value and logs it as sentinel_stabiliser_default= on the motion_output_taa row.
No game, no Wine."""
import tempfile
import unittest

import test_taa_sky_history as sky
import test_taa_thin_vote as vote

ROOT, TAA = sky.ROOT, sky.TAA


class SentinelStabiliserDefaultLaunch(unittest.TestCase):
    # The thin vote's launcher harness (dry run, never launches; no --vanilla added), without inheriting its tests.
    launch = vote.ThinVoteLaunch.launch
    env = vote.ThinVoteLaunch.env

    NAME = 'X3M_TAA_SENTINEL_STABILISER'
    MARKER = 'X3M_TAA_SENTINEL_STABILISER_DEFAULT'

    def test_default_off_with_taa_overrides_an_inherited_value(self):
        with tempfile.TemporaryDirectory() as directory:
            for inherited in (None, {self.NAME: '0.7', self.MARKER: '0'}):
                for args in ((), ('--taa-thin-region', '0.97'), ('--taa-thin-region', '0.97', '--taa-thin-region-gate', 'screen')):
                    env = self.env(directory, *TAA, *args, inherited=inherited)
                    self.assertEqual((env[self.NAME], env[self.MARKER]), ('0', '1'), (inherited, args))

    def test_explicit_values_send_marker_0(self):
        with tempfile.TemporaryDirectory() as directory:
            for value, sent in (('0.7', '0.7'), ('0', '0'), ('off', '0'), ('0.7,0', '0.7,0')):
                env = self.env(directory, *TAA, '--taa-thin-region', '0.97', '--taa-sentinel-stabiliser', value, inherited={self.MARKER: '1'})
                self.assertEqual((env[self.NAME], env[self.MARKER]), (sent, '0'), value)

    def test_without_taa_nothing_is_sent_and_inherited_values_are_dropped(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, '--motion-output', inherited={self.NAME: '0.7', self.MARKER: '1'})
            self.assertNotIn(self.NAME, env)
            self.assertNotIn(self.MARKER, env)

    def test_vanilla_sends_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            for args in (('--vanilla',), ('--vanilla', *TAA)):
                env = self.env(directory, *args, inherited={self.NAME: '0.7', self.MARKER: '1'})
                self.assertNotIn(self.NAME, env, args)
                self.assertNotIn(self.MARKER, env, args)


class SentinelStabiliserDefaultSource(unittest.TestCase):
    def test_dll_reads_the_marker_and_logs_it_on_the_configured_row(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        # The DLL fallback is unchanged: 0.7 under the camera gate when the variable is unset.
        self.assertIn('else if(taa_requested&&taa_thin_camera_gate)taa_sentinel[0]=.7f;', capture)
        self.assertIn('bool taa_sentinel_default = false;', capture)
        # The marker counts only after a valid value and only as exactly "1".
        self.assertIn('taa_sentinel_default=GetEnvironmentVariableW(L"X3M_TAA_SENTINEL_STABILISER_DEFAULT",sentinel_setting,32)==1&&sentinel_setting[0]==L\'1\';', capture)
        self.assertIn('configure_taa_sentinel(taa_sentinel[0],taa_sentinel[1],taa_sentinel_default)', capture)
        header = (ROOT / 'src/proxy/motion_output.h').read_text()
        self.assertIn('taa_sentinel_default_ = launcher_default;', header)
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        self.assertIn('sentinel_stabiliser=%.3f sentinel_emitter=%.3f ps30_slots=%u sentinel_stabiliser_default=%u"', motion)
        self.assertIn('unsigned(taa_sentinel_default_));', motion)

    def test_motion_runner_never_inherits_the_marker(self):
        runner = (ROOT / 'verification/probe/run_motion_output.py').read_text()
        self.assertIn("'X3M_TAA_SENTINEL_STABILISER_DEFAULT'):\n                env.pop(marker, None)", runner)


if __name__ == '__main__':
    unittest.main()
