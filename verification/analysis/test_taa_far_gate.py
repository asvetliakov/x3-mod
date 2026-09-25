"""Host tests of --taa-far-gate (X3M_TAA_FAR_GATE, docs/architecture/taa-mask-fold.md section 4.2 addendum): the far weight's
motion gate on the camera-gate resolve, camera (the camera-relative openness) by default on every modded --taa launch with
X3M_TAA_FAR_GATE_DEFAULT=1, screen (the screen speed gate before 2026-09-25) or camera explicitly with marker 0, nothing
without --taa or under --vanilla (an explicit value refused), an inherited shell value or marker never survives; the DLL
reads both (unset is camera), hands the gate to FrameInputs::far_camera_gate (c11.x of the hold program: a uniform select)
and logs the configured gate on the motion_output_taa row (far_gate= default=).
No game, no Wine."""
import tempfile
import unittest

import test_taa_sky_history as sky
import test_taa_thin_vote as vote

ROOT, TAA = sky.ROOT, sky.TAA


class FarGateLaunch(unittest.TestCase):
    # The thin vote's launcher harness (dry run, never launches; no --vanilla added), without inheriting its tests.
    launch = vote.ThinVoteLaunch.launch
    env = vote.ThinVoteLaunch.env

    NAME, MARKER = 'X3M_TAA_FAR_GATE', 'X3M_TAA_FAR_GATE_DEFAULT'

    def test_default_camera_with_taa_overrides_an_inherited_value(self):
        with tempfile.TemporaryDirectory() as directory:
            for inherited in (None, {self.NAME: 'screen', self.MARKER: '0'}):
                env = self.env(directory, *TAA, inherited=inherited)
                self.assertEqual((env[self.NAME], env[self.MARKER]), ('camera', '1'))

    def test_explicit_screen_and_camera_are_forwarded_with_marker_0(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('screen', 'camera'):
                env = self.env(directory, *TAA, '--taa-far-gate', value, inherited={self.MARKER: '1'})
                self.assertEqual((env[self.NAME], env[self.MARKER]), (value, '0'))

    def test_without_taa_nothing_is_sent_and_inherited_values_are_dropped(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, '--motion-output', '--no-taa', inherited={self.NAME: 'screen', self.MARKER: '1'})
            self.assertNotIn(self.NAME, env)
            self.assertNotIn(self.MARKER, env)

    def test_vanilla_sends_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, '--vanilla', inherited={self.NAME: 'screen', self.MARKER: '1'})
            self.assertNotIn(self.NAME, env)
            self.assertNotIn(self.MARKER, env)

    def test_other_values_missing_taa_and_vanilla_are_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('both', 'Camera', '1', ''):
                code, _, error = self.launch(directory, *TAA, '--taa-far-gate', value)
                self.assertNotEqual(code, 0, value)
                self.assertIn('--taa-far-gate', error)
            code, _, error = self.launch(directory, '--motion-output', '--no-taa', '--taa-far-gate', 'screen')
            self.assertNotEqual(code, 0)
            self.assertIn('--taa-far-gate requires --taa', error)
            code, _, error = self.launch(directory, '--vanilla', '--taa-far-gate', 'screen')
            self.assertNotEqual(code, 0)
            self.assertIn('--taa-far-gate cannot be combined with --vanilla', error)


class FarGateSource(unittest.TestCase):
    def test_dll_reads_the_setting_and_logs_the_configured_gate(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('GetEnvironmentVariableW(L"X3M_TAA_FAR_GATE"', capture)
        self.assertIn('bool taa_far_camera_gate = true, taa_far_gate_given = false, taa_far_gate_default = false;', capture)
        # The launcher's marker counts only with camera and only as exactly "1".
        self.assertIn('taa_far_gate_default=taa_far_gate_given&&taa_far_camera_gate&&GetEnvironmentVariableW(L"X3M_TAA_FAR_GATE_DEFAULT",setting,32)==1&&setting[0]==L\'1\';', capture)
        self.assertIn('taa_far_gate_setting invalid=1 reason=too_long length=%lu', capture)
        self.assertIn('log("taa_far_gate_setting invalid=1");', capture)
        self.assertIn('configure_far_gate(taa_far_camera_gate,taa_far_gate_given,taa_far_gate_default)', capture)
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        # The creation row: the configured gate (camera only on the camera-gate resolve) and the launcher default.
        self.assertIn('ps30_slots=%u far_gate=%s default=%u far_clip=%s far_clip_default=%u"', motion)
        self.assertIn('far_camera_gate ? "camera" : "screen", unsigned(far_camera_gate && taa_far_gate_default_)', motion)
        self.assertIn('const bool far_camera_gate = taa_far_camera_gate_ && taa_thin_camera_gate_ && taa_thin_weight_ > 0.f;', motion)
        # The ignore row only for an explicit camera (marker 0), never for the launcher's default.
        self.assertIn('if (SUCCEEDED(hr) && taa_far_gate_given_ && !taa_far_gate_default_ && taa_far_camera_gate_ && !far_camera_gate && taa_far_weight_ > 0.f)', motion)
        self.assertIn('motion_output_taa_far_gate device=%llu requested=camera configured=screen reason=%s"', motion)
        # A box-target refusal later in the session falls back to the far program: its row says so.
        self.assertIn('reason=box_target create=%08lx bilinear=%u history_taps=%u thin_region=%.4f effect=thin_region_off far_gate=screen far_clip=3x3"', motion)
        self.assertIn('in.far_camera_gate = taa_far_camera_gate_;', motion)

    def test_pass_and_program_select_the_gate_on_c11_x(self):
        header = (ROOT / 'src/renderer/temporal_pass.h').read_text()
        self.assertIn('bool far_camera_gate = true;', header)
        source = (ROOT / 'src/renderer/temporal_pass.cpp').read_text()
        self.assertIn('hold_constants[4]={in.far_camera_gate?0.f:1.f,', source)
        resolve = (ROOT / 'src/temporal/resolve.hlsl').read_text()
        self.assertIn('farOpen = holdGate.x > 0.5 ? farOpen : openC;', resolve)
        # The screen gate stays the far program's (outside X3M_REGION_HOLD) one expression.
        self.assertIn('float farOpen = 1 - saturate((speed - flicker.z) * flicker.w);', resolve)

    def test_motion_runner_clears_the_inherited_pair(self):
        runner = (ROOT / 'verification/probe/run_motion_output.py').read_text()
        self.assertIn("env.pop('X3M_TAA_FAR_GATE', None)", runner)
        self.assertIn("'X3M_TAA_FAR_GATE_DEFAULT'", runner)


if __name__ == '__main__':
    unittest.main()
