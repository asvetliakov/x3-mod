"""Host tests of the region hold (A', docs/architecture/taa-plan-lifted-slot-cap.md step 1) as the thin region's only camera-gate
path since 2026-09-24: --taa-region-hold is refused by name, X3M_TAA_REGION_HOLD is never forwarded and an inherited shell value
never survives; the DLL ignores a stale value with one line, turns the thin region off when the camera-gate programs are missing
(one line, no fallback program set) and logs the mask-target count; the hold programs are recorded and the removed dilated-chain
programs are gone; with the mask fold (2026-09-25, docs/architecture/taa-mask-fold.md) the camera mask programs and the sentinel
stabiliser's separable box twins are gone too and the camera gate draws no mask. No game, no Wine."""
import json
import re
import tempfile
import unittest

import test_taa_sky_history as sky

ROOT, TAA = sky.ROOT, sky.TAA

# The programs that existed only for the removed --taa-region-hold off path (the dilated camera chain).
REMOVED = ('temporal-resolve-far-camera', 'temporal-resolve-far-camera-taps16', 'temporal-thin-box', 'temporal-thin-box-rows',
           'temporal-thin-box-columns',
           # The mask fold (2026-09-25): the camera mask's three programs and the stabiliser's separable twins.
           'temporal-line-mask-camera', 'temporal-line-mask-camera-depth', 'temporal-line-mask-camera-depth-thin',
           'temporal-thin-box-rows-hold', 'temporal-thin-box-columns-hold')
HOLD = ('temporal-resolve-far-camera-hold', 'temporal-thin-box-hold', 'temporal-thin-box-rows-half', 'temporal-thin-box-columns-half')


class RegionHoldLaunch(unittest.TestCase):
    launch = sky.SkyHistoryLaunch.launch
    env = sky.SkyHistoryLaunch.env

    def test_never_forwarded_and_an_inherited_value_is_dropped(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertNotIn('X3M_TAA_REGION_HOLD', self.env(directory, *TAA))
            self.assertNotIn('X3M_TAA_REGION_HOLD', self.env(directory, *TAA, inherited={'X3M_TAA_REGION_HOLD': 'off'}))
            self.assertNotIn('X3M_TAA_REGION_HOLD', self.env(directory, '--motion-output', inherited={'X3M_TAA_REGION_HOLD': 'on'}))

    def test_the_removed_option_is_refused_by_name(self):
        with tempfile.TemporaryDirectory() as directory:
            for args in ((*TAA, '--taa-region-hold', 'on'), (*TAA, '--taa-region-hold', 'off'), (*TAA, '--taa-region-hold'),
                         ('--motion-output', '--taa-region-hold', 'off')):
                code, _, error = self.launch(directory, *args)
                self.assertNotEqual(code, 0, args)
                self.assertIn('--taa-region-hold was removed on 2026-09-24', error)


class RegionHoldSource(unittest.TestCase):
    def test_dll_ignores_a_stale_value_and_configures_no_hold_switch(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('if(GetEnvironmentVariableW(L"X3M_TAA_REGION_HOLD",setting,32)>0)log("taa_region_hold_setting ignored=1 reason=removed");', capture)
        self.assertNotIn('taa_region_hold', capture.replace('taa_region_hold_setting', ''))
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        self.assertNotIn('configure_region_hold', motion)
        self.assertNotIn('thin_region_hold =', motion)
        self.assertNotIn('fallback=dilated', motion)
        # The refusal path: the thin region off with one line when the camera-gate programs are missing.
        self.assertIn('log("motion_output_taa_region_hold device=%llu unavailable=1 reason=%s create=%08lx bilinear=%u history_taps=%u thin_region=%.4f effect=thin_region_off",', motion)
        self.assertIn('taa_thin_weight_ = 0.f; taa_thin_camera_gate_ = false;', motion)
        # Refused box targets turn the thin region off in the pass (no screen-gate fallback), one row per failure.
        self.assertIn('reason=box_target create=%08lx bilinear=%u history_taps=%u thin_region=%.4f effect=thin_region_off', motion)
        self.assertIn('if (taa_->camera_gate_failed() != taa_box_refused_logged_) {', motion)
        # The slot cap stays on the pass-creation row; the mask-target count rides the first completed run's row.
        self.assertIn('thin_emissive=%.3f ps30_slots=%u', motion)
        self.assertIn('reason=%s region_hold=%u mask_targets=%u', motion)
        self.assertIn('taa_->line_mask_targets()', motion)

    def test_pass_has_no_dilated_camera_path(self):
        header = (ROOT / 'src/renderer/temporal_pass.h').read_text()
        source = (ROOT / 'src/renderer/temporal_pass.cpp').read_text()
        for gone in (r'\bconfigure_region_hold\b', r'\bregion_hold_available\b', r'\bfar_camera_\b', r'\bfar_camera16_\b', r'\bthin_box_\b',
                     r'\bthin_box_rows_\b', r'\bthin_box_columns_\b', r'\bbool thin_region_hold\b'):
            self.assertIsNone(re.search(gone, header + source), gone)
        self.assertIn('history_taps_ != 16', header)  # the camera gate has no 16-tap program
        self.assertIn('const UINT draws=thin_on?3:1;final_mask=thin_on?0:1;', source)  # the screen-gate chain; the camera gate draws no mask
        self.assertIn('const bool mask_draws=far_on&&!camera;', source)
        self.assertIn('camera_gate_available() const noexcept { return far_available() && far_camera_hold_ != nullptr && thin_box_hold_ != nullptr && history_taps_ != 16 && render_targets_ >= 3; }', header)
        self.assertIn('if(camera_requested&&boxes_failed_)region_off();', source)
        self.assertIn('boxes_failed_=true;boxes_result_=boxes;camera=false;region_off();', source)
        for gone in ('configure_sentinel', 'sentinel_available', 'line_mask_camera_', 'thin_box_rows_hold_', 'sentinel_strength'):
            self.assertNotIn(gone, header + source, gone)

    def test_hold_programs_are_recorded_and_the_dilated_chain_programs_are_gone(self):
        for name in HOLD:
            record = json.loads((ROOT / f'verification/results/{name}-program.json').read_text())
            self.assertEqual(record['target'], 'ps_3_0', name)
        for name in REMOVED:
            self.assertFalse((ROOT / f'verification/results/{name}-program.json').exists(), name)
            self.assertFalse((ROOT / 'src/renderer' / (name.replace('-', '_') + '_program_inc.h')).exists(), name)
        generator = (ROOT / 'tools/shaders/generate_rigid_motion_pixel.py').read_text()
        for name in REMOVED:
            self.assertNotIn(f"'{name.replace('-', '_')}':", generator, name)


if __name__ == '__main__':
    unittest.main()
