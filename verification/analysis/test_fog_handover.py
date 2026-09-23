"""Fog hand-over after a sector change (docs/architecture/fog-handover.md, "Implementation"): the cold-start
readiness step against the warm ramp, the cold fill's single whole-atlas latch, the docked view's parent walk, the
R3 prefill (global-list walk, stall gate, confirm/discard, cache prefill) and the cold-step card arming
(verification/probe/fog_handover_host.cpp, native clang++, stepped cache, synthetic engine memory), then the
production wiring and the launcher switches. No Wine, no D3D.
"""
import re, shutil, subprocess, tempfile, unittest
from pathlib import Path
from verification.analysis import test_volumetric_fog

ROOT = Path(__file__).resolve().parents[2]
SOURCES = [ROOT / 'verification/probe/fog_handover_host.cpp', ROOT / 'src/fog/fog_density_cache.cpp', ROOT / 'src/fog/fog_density_generator.cpp']
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-pthread',
         '-I', str(ROOT / 'verification/probe'), '-I', str(ROOT / 'src/proxy')]


def fields(text, tag):
    line = re.search(r'^%s (.*)$' % tag, text, re.M).group(1)
    return {k: float(v) for k, v in re.findall(r'(\w+)=([-0-9.e+]+)', line)}


class FogHandoverHost(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('clang++')
        if compiler is None: raise RuntimeError('clang++ is required')
        cls.temporary = tempfile.TemporaryDirectory(prefix='x3-fog-handover-')
        tool = Path(cls.temporary.name) / 'handover-host'
        subprocess.run([compiler] + FLAGS + [str(s) for s in SOURCES] + ['-o', str(tool)], check=True, capture_output=True)
        done = subprocess.run([str(tool)], capture_output=True, timeout=600)
        cls.returncode, cls.text = done.returncode, done.stdout.decode()

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_all_checks_pass(self):
        self.assertEqual((self.returncode, re.findall(r'^CHECK (\S+) FAIL', self.text, re.M)), (0, []), self.text[-2000:])
        self.assertRegex(self.text, r'RESULT PASS checks=8\d failures=0')
        self.assertNotRegex(self.text, r'differing_bytes=[1-9]')

    def test_readiness_step_cold_versus_warm(self):
        cold, ramp, warm = fields(self.text, 'COLD_STEP'), fields(self.text, 'COLD_RAMP'), fields(self.text, 'WARM_RAMP')
        self.assertEqual(cold['resident_frame'], cold['ready_frame'])  # cold start: stepped in the resident frame
        self.assertIn(ramp['frames'], (89, 90))  # switches off: the legacy 90-frame ramp
        self.assertIn(warm['frames'], (89, 90))  # warm refill with the step on: still ramped
        self.assertLessEqual(warm['largest_step'], 1 / 90 + 1e-6)
        for name in ('warm_refill_reports_nothing', 'invalidate_is_a_cold_start_and_steps', 'cold_step_report_due_once_in_that_frame'):
            self.assertIn('CHECK %s PASS' % name, self.text)

    def test_cold_fill_budget_bypass_exactly_one_latch(self):
        latch, report, fill = fields(self.text, 'COLD_FILL_LATCH'), fields(self.text, 'COLD_FILL_REPORT'), fields(self.text, 'COLD_FILL')
        self.assertEqual(fill['generated'], fill['far_first_nodes'])  # the far need box alone before the latch
        self.assertEqual(fill['held'], 1)
        self.assertEqual((latch['far_rects'], latch['far_bytes'], latch['whole'], latch['fine_rects']), (1, 1032 * 516 * 8, 1, 0))
        self.assertGreater(latch['far_bytes'], latch['budget'])
        self.assertEqual((report['latches'], report['whole'], report['frames']), (1, 1, 2))
        self.assertGreater(fields(self.text, 'COLD_RAMP')['far_latches'], 1)  # budgeted: several latches
        for name in ('cold_fill_later_latches_within_the_budget', 'cold_fill_warm_jump_stays_budgeted_and_ramped',
                     'cold_fill_switched_off_releases_the_hold', 'cold_fill_after_gpu_reset_one_whole_latch_then_resident'):
            self.assertIn('CHECK %s PASS' % name, self.text)

    def test_cold_fill_review_edges(self):
        for name in ('hold_invalidate_one_whole_latch_of_the_new_identity', 'hold_invalidate_settles_to_the_new_field', 'lost_whole_latch_is_taken_again',
                     'whole_latch_waits_for_the_need_box', 'threaded_hold_then_one_whole_latch', 'threaded_far_staging_not_offered_while_unpublished',
                     'threaded_worker_woken_after_the_latch_settles'):
            self.assertIn('CHECK %s PASS' % name, self.text)
        self.assertEqual(fields(self.text, 'LOST_WHOLE')['wholes'], 2)
        threaded = fields(self.text, 'THREADED_COLD_FILL')
        self.assertEqual((threaded['wholes'], threaded['far_views_before_latch'], threaded['idle']), (1, 0, 1))
        self.assertEqual(threaded['resident'], threaded['ready'])

    def test_prefill_walk_gate_decision_and_cache(self):
        for name in ('walk_accepts_the_sector_at_the_tail', 'walk_accepts_the_eighth_node_within_25_reads', 'walk_is_cut_at_eight_nodes',
                     'walk_stops_at_the_list_head', 'walk_empty_list_is_the_head', 'walk_skips_a_cut_scene_space_subtype', 'walk_refuses_a_freed_object',
                     'walk_refuses_a_sector_without_scene', 'walk_refuses_the_last_ready_id', 'walk_no_manager', 'walk_unreadable_manager',
                     'walk_misaligned_node', 'walk_unreadable_link', 'walk_table_loading', 'walk_index_out_of_range', 'walk_bad_record',
                     'walk_never_exceeds_25_reads', 'gate_polls_only_in_a_stall_and_once_per_250ms', 'decide_confirms_the_same_key_and_discards_the_rest',
                     'prefill_fills_the_far_need_box_at_the_origin_and_holds', 'prefill_confirmed_configure_keeps_the_fill',
                     'prefill_hands_over_after_three_extension_slabs_and_the_latch', 'prefill_settles_to_the_destination_field', 'prefill_discard_starts_a_new_fill'):
            self.assertIn('CHECK %s PASS' % name, self.text)
        self.assertLessEqual(int(re.search(r'^PREFILL_WALK worst_reads=(\d+)', self.text, re.M).group(1)), 25)
        eighth = re.search(r'^PREFILL_WALK case=eighth status=(\w+) steps=(\d+) reads=(\d+)', self.text, re.M)
        self.assertEqual((eighth.group(1), int(eighth.group(2))), ('found', 8))
        ninth = re.search(r'^PREFILL_WALK case=ninth status=(\w+) steps=(\d+)', self.text, re.M)
        self.assertEqual((ninth.group(1), int(ninth.group(2))), ('bound', 8))

    def test_cold_step_arms_cards_in_the_same_frame(self):
        for name in ('cold_step_arms_the_cards_in_the_same_frame', 'cold_step_arming_faults_when_the_frame_fails', 'cold_step_arming_respects_refusal_and_inactive'):
            self.assertIn('CHECK %s PASS' % name, self.text)

    def test_parent_walk_depths_and_failures(self):
        for depth in (1, 2, 3):
            self.assertIn('CHECK walk_depth%d_found_ready_same_identity PASS' % depth, self.text)
            w = re.search(r'^WALK depth=%d reads_walk=(\d+) reads_legacy=(\d+)' % depth, self.text, re.M)
            self.assertEqual(int(w.group(1)) - int(w.group(2)), 2 * depth + 1)
        for name in ('walk_depth0_direct_match_without_walk', 'walk_depth4_exceeds_the_bound_and_refuses', 'walk_other_sector_refuses',
                     'walk_null_parent_refuses', 'walk_misaligned_parent_refuses_without_reading_it', 'walk_unreadable_object_refuses',
                     'walk_cycle_is_bounded', 'walk_direct_match_reads_exactly_the_legacy_reads', 'walk_span_reports_once_per_span_and_outcome'):
            self.assertIn('CHECK %s PASS' % name, self.text)


class FogHandoverWiring(unittest.TestCase):
    def test_production_wiring(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('sector_background::sample(read,volumetric_fog_docked?sector_background::anchor_walk_limit:0u)', capture)
        self.assertIn('volumetric_fog_handover_step=volumetric_fog_range_stored && fog_default_on(L"X3M_FOG_HANDOVER_STEP");', capture)
        self.assertIn('volumetric_fog_handover_coldfill=volumetric_fog_range_stored && fog_default_on(L"X3M_FOG_HANDOVER_COLDFILL");', capture)
        self.assertIn('volumetric_fog_docked=volumetric_fog_requested && fog_default_on(L"X3M_FOG_DOCKED");', capture)
        self.assertIn('configure_volumetric_fog_handover(volumetric_fog_handover_step,volumetric_fog_handover_coldfill);', capture)
        fog_pass = (ROOT / 'src/renderer/fog_pass.cpp').read_text()
        self.assertLess(fog_pass.index('density_->set_handover(config.handover_step,config.handover_coldfill);'), fog_pass.index('density_->configure(identity);'))
        self.assertIn('density_status_.handover=density_->take_handover();', fog_pass)
        header = (ROOT / 'src/renderer/fog_pass.h').read_text()
        self.assertIn('bool handover_step=false,handover_coldfill=false;', header)  # fixtures and the options off: legacy
        fog = (ROOT / 'src/proxy/motion_output_fog_inc.h').read_text()
        self.assertIn('log("volumetric_fog_handover device=%llu frame=%llu step=%u coldfill=%u whole_atlas=%u', fog)
        # The cold step arms the cards in its own frame, after prepare and before the frame's cards.
        self.assertIn('if (h.due && h.step && fog_cards_replace_ && status.ready_far >= 1.f) fog_cards_.arm_on_cold_step();', fog)
        # R3: the confirmation owns the transit's gap; the poll sites and the stall gate.
        sample = fog[fog.index('void MotionOutput::volumetric_fog_sector_sample('):fog.index('void MotionOutput::volumetric_fog_prefill(')]
        self.assertLess(sample.index('fog_prefill_confirm(next, sample)'), sample.index('fog_density_epoch("sample_gap")'))
        self.assertIn('prefill == fog_prefill::Decision::None && !fog_prefill_.pending', sample)
        self.assertEqual(capture.count('if(volumetric_fog_prefill)fog_prefill_poll(ctx);'), 2)
        self.assertIn('if(volumetric_fog_prefill && !telemetry::enabled()){ctx->set(23,create_texture);ctx->set(26,create_vb);}', capture)
        self.assertIn('ctx.fog_prefill_gate.present(GetTickCount64())', capture)
        poll = capture[capture.index('void fog_prefill_poll(Device& ctx) {'):capture.index('void object_context(Device& ctx) {')]
        self.assertLess(poll.index('fog_prefill_gate.stalled(now)'), poll.index('GetCurrentThreadId()'))  # outside a stall: one compare
        self.assertLess(poll.index('GetCurrentThreadId()'), poll.index('fog_prefill_gate.take(now)'))  # another thread never takes the slot
        self.assertLess(poll.index('fog_prefill_gate.take(now)'), poll.index('GetLastError()'))
        self.assertIn('action = "current_key"', fog)
        self.assertIn('SetLastError(value)', poll)
        self.assertIn('volumetric_fog_prefill=volumetric_fog_range_stored && fog_default_on(L"X3M_FOG_HANDOVER_PREFILL");', capture)
        self.assertIn('sector_background::anchor_refused(s)', (ROOT / 'src/proxy/fog_sector_policy.h').read_text())
        # The card mask still requires full far readiness and a drawable density frame (the invariant is unchanged).
        self.assertIn('fog_->density_status().ready_far >= 1.f && fog_->density_drawable(in.params.world.origin)', fog)

    def test_launcher_switches(self):
        launcher = test_volumetric_fog.FogLauncherTests()
        base, stored = launcher.BASE, ('--volumetric-fog', '--volumetric-fog-range', 'stored')
        status, output, error = launcher.launch(*base, *stored, environment={'X3M_FOG_HANDOVER_STEP': '0', 'X3M_FOG_DOCKED': '0'})
        self.assertEqual(status, 0, error)
        for name in ('X3M_FOG_HANDOVER_STEP', 'X3M_FOG_HANDOVER_COLDFILL', 'X3M_FOG_HANDOVER_PREFILL', 'X3M_FOG_DOCKED'):
            self.assertIn('"%s": "1"' % name, output)  # default on; an inherited value does not decide
        status, output, error = launcher.launch(*base, *stored, '--no-fog-handover-step', '--no-fog-handover-coldfill', '--no-fog-handover-prefill', '--no-fog-docked')
        self.assertEqual(status, 0, error)
        for name in ('X3M_FOG_HANDOVER_STEP', 'X3M_FOG_HANDOVER_COLDFILL', 'X3M_FOG_HANDOVER_PREFILL', 'X3M_FOG_DOCKED'):
            self.assertIn('"%s": "0"' % name, output)
        status, output, error = launcher.launch(*base, *stored, '--fog-handover-step', '--fog-docked')
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_FOG_HANDOVER_STEP": "1"', output)
        status, _, error = launcher.launch(*base, '--volumetric-fog', '--no-fog-handover-coldfill')
        self.assertNotEqual(status, 0)
        self.assertIn('require --volumetric-fog-range stored', error)
        status, _, error = launcher.launch(*base, '--no-fog-docked')
        self.assertNotEqual(status, 0)
        self.assertIn('require --volumetric-fog', error)
        status, output, error = launcher.launch(*base, '--volumetric-fog', '--no-fog-docked')  # legacy range: docked applies
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_FOG_DOCKED": "0"', output)


if __name__ == '__main__':
    unittest.main()
