"""Host tests of --taa-far-clip (X3M_TAA_FAR_CLIP, docs/architecture/taa-mask-fold.md section 4.2 addendum "far clip"): the
history clip of far pixels (farw * openC > 0) outside the thin region on the camera-gate resolve, 7x7 (the min / max of the current
colour) by default on every modded --taa launch with X3M_TAA_FAR_CLIP_DEFAULT=1, 3x3 (the variance clip before 2026-09-25) or
7x7 explicitly with marker 0, nothing without --taa or under --vanilla (an explicit value refused), an inherited shell value or
marker never survives; the DLL reads both (unset is 7x7), hands the threshold to FrameInputs::far_clip (c13.z of the hold
program: 0 for 7x7, 2 for 3x3) and logs the configured clip on the motion_output_taa row (far_clip= far_clip_default=). The far ramp's defaults
F0 / F1 are 60 / 68 in the launcher and the DLL.
No game, no Wine."""
import tempfile
import unittest

import test_taa_sky_history as sky
import test_taa_thin_vote as vote

ROOT, TAA = sky.ROOT, sky.TAA


class FarClipLaunch(unittest.TestCase):
    # The thin vote's launcher harness (dry run, never launches; no --vanilla added), without inheriting its tests.
    launch = vote.ThinVoteLaunch.launch
    env = vote.ThinVoteLaunch.env

    NAME, MARKER = 'X3M_TAA_FAR_CLIP', 'X3M_TAA_FAR_CLIP_DEFAULT'

    def test_default_7x7_with_taa_overrides_an_inherited_value(self):
        with tempfile.TemporaryDirectory() as directory:
            for inherited in (None, {self.NAME: '3x3', self.MARKER: '0'}):
                env = self.env(directory, *TAA, inherited=inherited)
                self.assertEqual((env[self.NAME], env[self.MARKER]), ('7x7', '1'))
                # The far ramp's new footprints travel in the far stabiliser's default value.
                self.assertEqual(env['X3M_TAA_FAR_STABILISER'], '0.985,0,60,68,0.03,0.25')

    def test_explicit_values_are_forwarded_with_marker_0(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('3x3', '7x7'):
                env = self.env(directory, *TAA, '--taa-far-clip', value, inherited={self.MARKER: '1'})
                self.assertEqual((env[self.NAME], env[self.MARKER]), (value, '0'))

    def test_the_old_far_ramp_can_still_be_requested(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, *TAA, '--taa-far-stabiliser', '0.985,0,80,130')
            self.assertEqual(env['X3M_TAA_FAR_STABILISER'], '0.985,0,80,130,0.03,0.25')

    def test_without_taa_nothing_is_sent_and_inherited_values_are_dropped(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, '--motion-output', '--no-taa', inherited={self.NAME: '3x3', self.MARKER: '1'})
            self.assertNotIn(self.NAME, env)
            self.assertNotIn(self.MARKER, env)

    def test_vanilla_sends_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, '--vanilla', inherited={self.NAME: '3x3', self.MARKER: '1'})
            self.assertNotIn(self.NAME, env)
            self.assertNotIn(self.MARKER, env)

    def test_other_values_missing_taa_and_vanilla_are_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('5x5', '7X7', '1', ''):
                code, _, error = self.launch(directory, *TAA, '--taa-far-clip', value)
                self.assertNotEqual(code, 0, value)
                self.assertIn('--taa-far-clip', error)
            code, _, error = self.launch(directory, '--motion-output', '--no-taa', '--taa-far-clip', '3x3')
            self.assertNotEqual(code, 0)
            self.assertIn('--taa-far-clip requires --taa', error)
            code, _, error = self.launch(directory, '--vanilla', '--taa-far-clip', '3x3')
            self.assertNotEqual(code, 0)
            self.assertIn('--taa-far-clip cannot be combined with --vanilla', error)


class FarClipSource(unittest.TestCase):
    def test_dll_reads_the_setting_and_logs_the_configured_clip(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('GetEnvironmentVariableW(L"X3M_TAA_FAR_CLIP"', capture)
        self.assertIn('bool taa_far_clip_7x7 = true, taa_far_clip_given = false, taa_far_clip_default = false;', capture)
        # The launcher's marker counts only with 7x7 and only as exactly "1".
        self.assertIn('taa_far_clip_default=taa_far_clip_given&&taa_far_clip_7x7&&GetEnvironmentVariableW(L"X3M_TAA_FAR_CLIP_DEFAULT",setting,32)==1&&setting[0]==L\'1\';', capture)
        self.assertIn('taa_far_clip_setting invalid=1 reason=too_long length=%lu', capture)
        self.assertIn('log("taa_far_clip_setting invalid=1");', capture)
        self.assertIn('configure_far_clip(taa_far_clip_7x7,taa_far_clip_given,taa_far_clip_default)', capture)
        # The far ramp's DLL defaults (unset, and the fields a shorter X3M_TAA_FAR_STABILISER leaves out).
        self.assertIn('float taa_far[6] = {0.f, 0.f, 60.f, 68.f, .03f, .25f};', capture)
        self.assertIn('float v[6]={0.f,0.f,60.f,68.f,.03f,.25f};', capture)
        header = (ROOT / 'src/proxy/motion_output.h').read_text()
        self.assertIn('taa_far_f0_ = 60.f, taa_far_f1_ = 68.f', header)
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        self.assertIn('ps30_slots=%u far_gate=%s default=%u far_clip=%s far_clip_default=%u"', motion)
        self.assertIn('far_clip ? "7x7" : "3x3", unsigned(far_clip && taa_far_clip_default_)', motion)
        self.assertIn('const bool far_clip = taa_far_clip_7x7_ && taa_thin_camera_gate_ && taa_thin_weight_ > 0.f && far_gate_on;', motion)
        # The ignore row only for an explicit 7x7 (marker 0), never for the launcher's default.
        self.assertIn('if (SUCCEEDED(hr) && taa_far_clip_given_ && !taa_far_clip_default_ && taa_far_clip_7x7_ && !far_clip)', motion)
        self.assertIn('motion_output_taa_far_clip device=%llu requested=7x7 configured=3x3 reason=%s"', motion)
        self.assertIn('in.far_clip = taa_far_clip_7x7_ ? x3::temporal::kFarClipThreshold : x3::temporal::kFarClipOff;', motion)

    def test_pass_and_program_carry_the_threshold_on_c13_z(self):
        resolve_h = (ROOT / 'src/temporal/resolve.h').read_text()
        # 0: every pixel with any far weight (strict >); 2: above any product of two openness values, the 3x3 clip.
        self.assertIn('constexpr float kFarClipThreshold = 0.f, kFarClipOff = 2.f;', resolve_h)
        header = (ROOT / 'src/renderer/temporal_pass.h').read_text()
        self.assertIn('float far_clip = x3::temporal::kFarClipThreshold;', header)
        source = (ROOT / 'src/renderer/temporal_pass.cpp').read_text()
        self.assertIn('far_gate_constants[4]={far_constants[0],far_constants[1],far_on?in.far_clip:x3::temporal::kFarClipOff,0.f};', source)
        self.assertIn('!x3::temporal::valid_far_clip(in.far_clip)', source)
        resolve = (ROOT / 'src/temporal/resolve.hlsl').read_text()
        # Gated on farw * openC (the far weight's gate): a far mover keeps the 3x3 bound.
        self.assertIn('bool farClip = region <= 0 && farw * openC > farGate.z;', resolve)
        # The 7x7 taps stay on the dynamic branch the camera term's share already takes.
        self.assertIn('else [branch] if (stabilise.b > stabilise.a || farClip) {', resolve)
        self.assertIn('clipped = farClip ? boxed : clipped;', resolve)

    def test_embedded_hold_program_is_compiled_from_the_current_resolve(self):
        # The provenance of the embedded camera-gate program names the resolve.hlsl it was compiled from: a source edit without
        # the regeneration (generate_rigid_motion_pixel.py, under the Wine lock) would ship the previous far-clip semantics.
        import hashlib, json
        record = json.loads((ROOT / 'verification/results/temporal-resolve-far-camera-hold-program.json').read_text())
        source = hashlib.sha256((ROOT / 'src/temporal/resolve.hlsl').read_bytes()).hexdigest()
        self.assertEqual(record['includes']['src/temporal/resolve.hlsl'], source)
        header = (ROOT / 'src/renderer/temporal_resolve_far_camera_hold_program_inc.h').read_bytes()
        self.assertEqual(record['header_sha256'], hashlib.sha256(header).hexdigest())

    def test_thin_vote_frame_row_carries_the_closest_unvoted_fraction(self):
        header = (ROOT / 'src/proxy/motion_output.h').read_text()
        self.assertIn('float max_unvoted = 0.f;', header)
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        self.assertIn('else if (fraction > f.max_unvoted) f.max_unvoted = fraction;', motion)
        self.assertIn('voted=%u min_alpha=%.4f max_unvoted_fraction=%.4f unreadable=%u', motion)
        self.assertIn('f.voted, double(f.min_alpha), double(f.max_unvoted), f.unreadable,', motion)

    def test_motion_runner_clears_the_inherited_pair(self):
        runner = (ROOT / 'verification/probe/run_motion_output.py').read_text()
        self.assertIn("env.pop('X3M_TAA_FAR_CLIP', None)", runner)
        self.assertIn("'X3M_TAA_FAR_CLIP_DEFAULT'", runner)
        # Every earlier launcher marker is still cleared.
        for marker in ('X3M_TAA_THIN_VOTE_DEFAULT', 'X3M_FADE_RT2_OWNER_DEFAULT', 'X3M_LOD_OCCLUSION_DEFAULT', 'X3M_TAA_BOX_RESOLUTION_DEFAULT',
                       'X3M_ORIGINAL_FILL_DEFAULT', 'X3M_TAA_THIN_REGION_SOURCE_DEFAULT', 'X3M_SUN_OCCLUSION_DEFAULT', 'X3M_TAA_FAR_GATE_DEFAULT'):
            self.assertIn("'%s'" % marker, runner)


if __name__ == '__main__':
    unittest.main()
