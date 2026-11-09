"""The tracked native records of the weapon effects' step 1 (docs/architecture/weapon-effects.md 5.1; ledger
docs/verification/weapon-effects.md), written by
`python3 verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py --host native`:

- hook-summary.json, the hook fixture (verification/probe/weapon_effects_hook_fixture.cpp built and audited by
  build_weapon_effects_hook.py, run on the Windows side through native_host.py): every check passed, the review's
  cases present, the build audit clean (no x87, no undefined symbol, no call in the handler entry points), and the
  record bound to the fixture and production sources it compiled (their hashes against the tree);
- summary.json, the D3D fixture: passed, bound to its production sources, the structural gates of the step.
No Wine, no game: the records are read, not produced.
"""
import hashlib
import json
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RESULTS = ROOT / 'verification/results/native-windows/weapon-effects'
sys.path.insert(0, str(ROOT / 'verification/probe'))


def sha(path):
    return hashlib.sha256((ROOT / path).read_bytes()).hexdigest()


class WeaponEffectsHookRecord(unittest.TestCase):
    PATH = RESULTS / 'hook-summary.json'
    # The cases the review asked for (F7) and the brief's acceptance list; a record without one of them is stale.
    REQUIRED = {'changed_byte_refused', 'witness_mismatch_refused', 'prologue_mismatch_refused', 'tap_byte_mismatch_refused',
                'forward_callee_eax_ecx_edx_ebx_ebp', 'forward_callee_return_address', 'forward_callee_esp',
                'forward_callee_eflags', 'forward_frame_kept', 'skip_frame_kept', 'skip_no_callee',
                'shields_default_skips_native', 'shield_hit_skips_callee_hull_forwards', 'shield_hit_stage_and_forward', 'shield_hit_forward_callee_state',
                'tap_callee_esi_ebx_edi_return', 'weapon_pos_check_zero_error', 'no_projectile_class_forwards_unrecorded',
                'no_bullet_extension_read', 'nested_dying_projectile_staged_marked', 'nested_dying_victim_forwards_marked',
                'record_shield_unread_tier_ship_class', 'shield_read_capacity_weighted', 'shield_read_large_capacity',
                'shield_read_energy_clamped', 'shield_sentinel_only_unread', 'shield_sentinel_ends_list',
                'shield_index_out_of_range_refused', 'shield_null_shield_extension_refused',
                'shield_null_extension_refused', 'shield_list_64_node_limit', 'shield_unshielded_class_skipped_bit20_read',
                'patch_tier_class_id_and_radius_fallback', 'event_cost_50_within_30us',
                'nested_death_retires_owner', 'overflow_keeps_owned_shield', 'observe_rollback_three_sites',
                'observe_rollback_second_site', 'rollback_on_failed_readback', 'late_claim_refused',
                'scope_sector_change_clears_owned', 'scope_loading_clears_owned', 'scope_load_epoch_clears_owned',
                'fixture_stack_margin', 'travel_dir_from_bullet_row2', 'travel_dir_fallback_zero_row_dead_node',
                'drained_shield_hit_forwards_as_hull_hit', 'shield_hit_above_drained_threshold_staged',
                'unread_shield_keeps_the_engine_rule', 'drained_threshold_boundary_326_327',
                # the setting's words (2026-11-08: the scratched families' names ignored beside shields)
                'mode_parse_ignores_scratched_families',
                # the hull family's hook (reached through install_at): the shields,hull mode's skip and forward
                'hull_mode_installed', 'hull_hit_skips_native_when_staged', 'hull_skip_frame_kept',
                'hull_mode_shield_hit_staged', 'hull_hit_forwards_when_not_owned', 'hull_hit_dying_object_forwards',
                'drained_hull_hit_staged', 'hull_hit_stage_and_forward', 'hull_missile_source_staged',
                'hull_asteroid_variant', 'hull_repair_tug_variant_by_override', 'hull_present_keeps_floor_then_releases',
                'hull_hit_forwards_without_hull_programs', 'overflow_census_first_then_oldest_staged',
                # the 1024-record ring (2026-11-07): the hidden owner's records first, the handler's cost when full
                'overflow_1024_hidden_owner_first_then_oldest', 'event_cost_50_full_ring_1024',
                'overflow_counts_evicted_visible',
                # step 3, the death sites (reached through install_at)
                'death_decision_seam', 'death_installed', 'death_forwards_when_not_owned',
                'death_skips_fighter_when_owned', 'death_skips_missile_when_owned', 'death_forwards_big_tier_census',
                'death_forwards_asteroid', 'death_forwards_rule_b_missile', 'death_area_bullet_forwards',
                'death_retires_owner_hits', 'death_nested_stack', 'death_present_keeps_ours_releases_census',
                'death_forwards_without_boom_programs', 'death_shutdown_restores_both',
                'death_mode_without_token_leaves_sites', 'death_byte_mismatch_refused', 'death_rollback_restores_both',
                'death_nested_after_return'}

    def setUp(self):
        self.record = json.loads(self.PATH.read_text())

    def test_every_check_passed(self):
        r = self.record
        self.assertTrue(r['passed'])
        self.assertFalse(r['game_launched'])
        self.assertIsNone(r['bottle'])
        self.assertEqual(r['host']['name'], 'native-windows')
        self.assertEqual(r['run']['exit_code'], 0)
        self.assertEqual(r['run']['result']['verdict'], 'PASS')
        self.assertEqual(r['run']['result']['failures'], 0)
        self.assertEqual(r['run']['result']['checks'], r['check_count'])
        self.assertEqual(len(r['checks']), r['check_count'])
        self.assertEqual(r['failed_checks'], [])
        self.assertTrue(all(c['passed'] for c in r['checks']))
        self.assertLessEqual(self.REQUIRED, {c['name'] for c in r['checks']})

    def test_build_audit(self):
        a = self.record['build']['audit']
        self.assertEqual((a['handler_undefined'], a['handler_x87'], a['module_imports_own_protect']), ([], [], []))
        self.assertEqual(a['handler_calls'], {'_x3m_weapon_event': [], '_x3m_weapon_tap': [], '_x3m_weapon_bolt_tap': [],
                                              '_x3m_weapon_muzzle': [], '_x3m_weapon_flight': [],
                                              '_x3m_weapon_death_area': [], '_x3m_weapon_death_other': []})
        self.assertTrue(a['stub_ok'] and a['stub_bolt_tap_ok'] and a['stub_muzzle_ok'] and a['stub_flight_ok'])
        # step 3: the death stubs pass EBP and end at the skip's ret (the big tiers' swap path left with the look layer);
        # both windows are the installed EXE's bytes
        self.assertEqual(a['stub_death_ok'], {'area': True, 'other': True})
        self.assertEqual(a['death_windows_exe'], {'area': True, 'other': True})
        self.assertGreaterEqual(a['engine_patch_lock_cmpxchg8b'], 1)
        # the stub (0x20 + the return address) and the handler's frame stayed inside the fixture's margin
        self.assertEqual(self.record['stack_depth']['bytes'], a['handler_frame_bytes'] + 0x24)

    def test_shield_read_and_event_cost(self):
        weighted = next(s for s in self.record['shield'] if s['case'] == 'weighted')
        self.assertLess(abs(weighted['fraction'] - weighted['hud']), 4e-5)   # the HUD's capacity-weighted ratio
        self.assertGreater(abs(weighted['fraction'] - weighted['mean_of_fractions']), 0.1)  # not the mean
        cost = self.record['event_cost']
        self.assertEqual((cost['events'], cost['shields']), (50, 6))
        self.assertLessEqual(cost['median_us'], 30.0)                          # the 30 us per 50 events target
        # the 1024-record ring: through the handler the stamped-hidden owner's 16 records go first, then the oldest
        # drawn record; 50 events on the full ring within the fixture's bound
        over = self.record['overflow']
        self.assertEqual((over['capacity'], over['stamped_hidden'], over['first_evicts_oldest_hidden'],
                          over['hidden_after_16'], over['drawn_kept_after_16'], over['evicted_after'], over['overflow'],
                          over['evicted_visible']),
                         (1024, 16, 1, 0, 1, 'oldest_drawn', 17, 1))
        full = self.record['event_cost_full']
        self.assertEqual((full['events'], full['capacity']), (50, 1024))
        self.assertLessEqual(full['median_us'], full['target_us'])
        self.assertEqual(full['target_us'], 150)

    def test_drained_shield_is_a_hull_hit(self):
        # Run237 Flight D: an absorbed hit below 0.5 % post-hit shield forwards as a hull hit; 400 / 65535 and an
        # unread shield stay the stage's shield hit (skipped)
        d = {x['case']: x for x in self.record['drained']}
        low, high, unread = d['low'], d['high'], d['unread']
        self.assertLess(low['q'], low['threshold'])
        self.assertEqual((low['role'], low['detail'], low['absorbed'], low['forwarded'], low['stage'], low['callee']),
                         ('hull_hit', 1, 1, 1, 0, 1))
        self.assertEqual((low['drained_delta'], low['forwarded_delta']), (1, 1))
        self.assertGreater(high['q'], low['threshold'])
        self.assertEqual((high['role'], high['detail'], high['stage'], high['callee']), ('shield_hit', 0, 1, 0))
        self.assertEqual((unread['read'], unread['role'], unread['stage'], unread['callee'], unread['drained_total_delta']),
                         (0, 'shield_hit', 1, 0, 1))
        b = d['boundary']                                                     # 326 drained, 327 not
        self.assertEqual((b['q_below'], b['role_below'], b['callee_below'], b['q_at'], b['role_at'], b['callee_at']),
                         (326, 'hull_hit', 1, 327, 'shield_hit', 0))

    def test_hull_mode_rows(self):
        rows = {x['case']: x for x in self.record['hull_mode']}
        self.assertEqual((rows['skip']['through'], rows['skip']['unabsorbed'], rows['skip']['skipped_delta'],
                          rows['skip']['forwarded_delta']), (1, 1, 2, 0))
        self.assertEqual((rows['present']['kept'], rows['present']['released']), (1, 1))

    def test_death_forward_and_nested(self):
        # the forward path reaches 0x00413610 with ECX = arg4, EDX = the killer (the window's loads), the flags, the
        # return after the site and ESP three arguments and the return below the frame; three records in one outer call
        f = self.record['death_forward_state']
        mask = 0xcd5
        self.assertEqual(int(f['eflags_entry'], 16) & mask, int(f['eflags_callee'], 16) & mask)
        self.assertEqual(int(f['esp_callee'], 16), int(f['esp_frame'], 16) - 16)
        self.assertEqual(int(f['return'], 16), int(f['site'], 16) + 5)
        self.assertEqual((int(f['ecx'], 16), int(f['edx'], 16)), (0x00a4a4a4, 0x00c0ffee))
        n = self.record['death_nested']
        self.assertEqual((n['records'], n['nested'], n['outer_entries']), (3, 2, 1))

    def test_forward_state(self):
        f = {k: int(v, 16) for k, v in self.record['forward_state'].items()}
        mask = 0xcd5  # CF PF AF ZF SF DF OF
        self.assertEqual(f['eflags_entry'] & mask, f['eflags_callee'] & mask)
        self.assertEqual(f['esp_callee'], f['esp_frame'] - 12)
        self.assertEqual(f['return'], f['site'] + 5)

    def test_bound_to_its_sources(self):
        import run_weapon_effects as runner
        source = self.record['source']
        self.assertEqual(self.record['sources'], list(runner.HOOK_SOURCES))
        self.assertIn('verification/probe/weapon_effects_hook_fixture.cpp', source['sha256'])
        self.assertRegex(source['commit'], r'^[0-9a-f]{40}(-dirty)?$')
        for path, recorded in source['sha256'].items():
            self.assertEqual(sha(path), recorded, path + ': changed since the record: rerun run_weapon_effects.py --host native')




class WeaponEffectsHookFlRecord(unittest.TestCase):
    """hook-fl-summary.json, the FL hook fixture (weapon_effects_hook_fixture_fl.cpp; docs/architecture/weapon-effects.md
    2.10): every check passed, every FL shim's slots exercised, the audit clean against X3FL.exe, the stack depth the
    audited handler frame plus the FL impact stub's 0x2c, bound to its sources."""
    PATH = RESULTS / 'hook-fl-summary.json'
    REQUIRED = {'every_window_bit_refused', 'bolt_tap_fixup_other_base_refused', 'muzzle_fixup_other_base_refused',
                'installed_all_sites_active', 'globals_bound_base_plus_rva', 'write_paths_atomic_except_object_tap',
                'shutdown_restores_all', 'rollback_restores_all', 'init_logs_one_row_per_site', 'late_claim_refused',
                'absorbed_piloted_clamped_total', 'impact_forward_reaches_callee_ecx_edx_return',
                'impact_forward_records_shield_hit_absorbed', 'impact_skip_no_callee_eax_zero',
                'impact_piloted_clamped_absorbed_staged', 'impact_ecx_edx_swapped_differs',
                'impact_piloted_drained_shields_hull_hit', 'tap1_hit_pos_slots_match', 'tap1_pos_slot_read',
                'tap1_hit_slot_read', 'tap2_hit_pos_slots_match', 'tap2_pos_slot_read', 'tap2_hit_slot_read',
                'object_tap_handler_records_ecx_object', 'muzzle_handler_slot_row_pos_orient', 'muzzle_skip_no_callee',
                'flight_forward_reaches_callee_bullet', 'flight_skip_no_callee',
                'death_forward_records_other_tag_reason_killer', 'death_skip_no_callee_eax_zero',
                'fl_bubble_walk_reads_node_body_0x144', 'fixture_stack_margin',
                # the projectile stage's batch submit (bullet-draw-batch.md section 5)
                'batch_latch_off_forwards_registers_stack_flags', 'batch_other_body_forwards_registers_stack_flags',
                'batch_enabled_body_skipped_eax1_no_callee', 'batch_rollback_restores_all',
                'shields_mode_claims_batch_and_object_tap', 'init_logs_projectile_site_claimed_once',
                'object_tap_registers_enabled_body_once',
                # the muzzle flash (muzzle-flash-look.md section 4): the shields mode claims the muzzle, its witness and
                # rollback; an enabled body's shot skipped under the latch, forwarded otherwise with every register,
                # the stack and EFLAGS; the tap's match to the ours record
                'shields_mode_claims_muzzle', 'shields_mode_muzzle_witness_refused',
                'shields_mode_muzzle_rollback_restores_all',
                'muzzle_enabled_body_latch_off_forwards_registers_stack_flags',
                'muzzle_other_body_forwards_registers_stack_flags', 'object_tap_matches_flash_to_ours_record'}

    def setUp(self):
        self.record = json.loads(self.PATH.read_text())

    def test_every_check_passed(self):
        r = self.record
        self.assertTrue(r['passed'])
        self.assertEqual((r['edition'], r['game_launched'], r['host']['name']), ('fl', False, 'native-windows'))
        self.assertEqual((r['run']['exit_code'], r['run']['result']['verdict'], r['run']['result']['failures']),
                         (0, 'PASS', 0))
        self.assertEqual(r['run']['result']['checks'], r['check_count'])
        self.assertEqual(r['failed_checks'], [])
        self.assertLessEqual(self.REQUIRED, {c['name'] for c in r['checks']})

    def test_build_audit_and_stack(self):
        a = self.record['build']['audit']
        self.assertEqual((a['handler_undefined'], a['handler_x87'], a['module_imports_own_protect']), ([], [], []))
        self.assertTrue(all(a['stub_ok'].values()) and len(a['stub_ok']) == 7)  # six shims and the batch stub
        self.assertEqual(len(a['handler_calls']), 6)
        self.assertTrue(all(not v for v in a['handler_calls'].values()))
        self.assertEqual(a['windows_match_exe'], {k: True for k in ('impact', 'tap1', 'tap2', 'bolt_tap', 'muzzle', 'flight',
                                                                     'death', 'batch')})
        self.assertGreaterEqual(a['engine_patch_lock_cmpxchg8b'], 1)
        self.assertTrue(self.record['stack_ok'])
        self.assertEqual(self.record['stack_depth']['bytes'], a['handler_frame_bytes'] + 0x2c)

    def test_bound_to_its_sources(self):
        import run_weapon_effects as runner
        self.assertEqual(self.record['sources'], list(runner.HOOK_FL_SOURCES))
        for path, recorded in self.record['source']['sha256'].items():
            self.assertEqual(sha(path), recorded, path + ': changed since the record: rerun run_weapon_effects.py --host native')


class WeaponEffectsD3DRecord(unittest.TestCase):
    PATH = RESULTS / 'summary.json'

    def setUp(self):
        self.record = json.loads(self.PATH.read_text())

    def test_projectile_gates(self):
        # docs/architecture/projectile-look.md "Implementation (2026-11-08)": the width rule at both fields of view, SETA
        # invariance, the far flag, the dot, nothing outside the quad, the 512-bolt budget at 5120x1440; FL 59 A: the
        # seam of two consecutive frames (no gap, the light uniform within 10 %) at 60 and 144 fps, the static frame
        rows = self.record['metrics']['projectile']
        width = [x for x in rows if x.get('case') == 'width']
        self.assertEqual(len(width), 8)
        self.assertTrue(all(abs(x['measured_px'] - x['rule_px']) <= 0.5 for x in width))
        seta = [x['px_per_game_ms'] for x in rows if x.get('case') == 'seta']
        self.assertLess(abs(seta[1] / seta[0] - 1.0), 0.03)
        flag = next(x for x in rows if x.get('case') == 'far_flag')
        self.assertAlmostEqual(flag['ratio'], 32.0, delta=0.5)
        self.assertEqual(next(x for x in rows if x.get('case') == 'quad')['outside'], 0)
        seam = [x for x in rows if x.get('case') == 'seam']
        self.assertEqual(sorted((x['fps'], x['tune'], x['camera_mps'], x['turn_deg']) for x in seam),
                         [(60, 'baked', 0, 0), (60, 'baked', 0, 1), (60, 'baked', 200, 0), (60, 'flown', 0, 0),
                          (144, 'baked', 0, 0), (144, 'flown', 0, 0)])
        for x in seam:
            self.assertLessEqual(abs(x['gap_px']), 0.05)
            self.assertLessEqual(x['spread'], 0.10)
            self.assertEqual(x['jumped'], 0)
        static = next(x for x in rows if x.get('case') == 'static')
        self.assertEqual(static['statics'], '0/1/0/1/0/0')
        self.assertEqual(static['jumped'], '0/0/0/0/0/1')
        self.assertEqual(static['held'], 1)
        cut = next(x for x in rows if x.get('case') == 'cut')
        self.assertLess(abs(cut['cut_length_px'] - cut['rule_px']), 0.01)
        endon = next(x for x in rows if x.get('case') == 'endon')
        self.assertLess(abs(endon['centre_ratio'] - 1.0), 0.02)  # straight-ahead fire at full core radiance
        self.assertEqual((endon['endon_energy'], endon['slow_energy'], endon['paused_energy']), (1.0, 1.0, 1.0))
        cap = next(x for x in rows if x.get('case') == 'cap')
        self.assertLess(abs(cap['centre_ratio'] / cap['want_energy'] - 1.0), 0.03)
        budget = next(x for x in rows if x.get('case') == 'budget')
        self.assertEqual((budget['bolts'], budget['drawn']), (512, 512))
        self.assertEqual((budget['statics'], budget['jumped']), (0, 0))
        self.assertLess(budget['cpu_us'], 100.0)
        self.assertLess(budget['gpu_us'], 50.0)
        self.assertLess(next(x for x in rows if 'attach' in x)['ps_slots'], 200)

    def test_muzzle_flash_gates(self):
        # docs/architecture/muzzle-flash-look.md section 6: the white extent 1.25 x the core's at the gun's depth and
        # 200 / 500 m (capped at 160 px at 4 m only) at +2 and +4 stops (the anti-lamp gate), the streak's length, the
        # dot past it, the wall-clock life at 60 and 144 fps, SETA 6 and the key's bounds, the pulse by its law, the budget
        rows = self.record['metrics']['flash']
        size = [x for x in rows if x.get('case') == 'size']
        self.assertEqual(len(size), 12)
        for x in size:
            self.assertLessEqual(abs(x['white_px'] - x['rule_white_px']), 0.25, x)
            # the anti-lamp invariant on both branches: white <= min(flash_core_mult x the core's, the 160 px cap)
            self.assertLessEqual(x['white_px'], min(1.25 * x['core_white_measured_px'], 160.0) + 0.25, x)
            if x['capped']:
                self.assertEqual(x['distance_m'], 4)
            elif x['flash_stops'] == 2:  # the core's 1 px floor applies to the flash too: the ratio at 500 m as well
                self.assertLessEqual(abs(x['ratio'] / 1.25 - 1.0), 0.10, x)
        gun = next(x for x in size if x['distance_m'] == 27 and x['flash_stops'] == 2)
        self.assertAlmostEqual(gun['ratio'], 1.25, delta=0.02)
        lamp = next(x for x in size if x['distance_m'] == 27 and x['flash_stops'] == 4)
        self.assertAlmostEqual(lamp['white_px'], gun['white_px'], delta=0.5)  # brightness does not grow it
        streak = {x['distance_m']: x for x in rows if x.get('case') == 'streak'}
        for d in (27, 500):
            self.assertLessEqual(abs(streak[d]['length_px'] - streak[d]['rule_px']), 1.0)
        self.assertEqual((streak[2000]['folded'], streak[2000]['dot']), (1, 1))
        life = {x['name']: x for x in rows if x.get('case') == 'life'}
        self.assertEqual({k: v['drawn'] for k, v in life.items()},
                         {'life80_frame17': 5, 'life80_frame7': 12, 'life80_seta6_step100': 5, 'life400_frame17': 24,
                          'life16_frame17': 1, 'tapped_one_step_late': 4, 'tapped_one_step_late_seta6': 4,
                          'tapped_late_backdate_cap_rate0': 1, 'not_ours': 0, 'tapped_late': 0,
                          'record_other_shooter': 0})
        # the birth set back by min(game age / SETA rate, 1 s): 100 / 6 ms under SETA 6, the 1 s cap at 1.5 s late
        self.assertAlmostEqual(life['tapped_one_step_late_seta6']['back_us'], 100e3 / 6.0, delta=1.0)
        self.assertAlmostEqual(life['tapped_late_backdate_cap_rate0']['back_us'], 1e6, delta=1.0)
        self.assertEqual(life['record_other_shooter']['lost'], 1)  # the record's shooter compared on the first frame
        for x in life.values():  # F = 1 on the first frame, then 1 - age / life (wall clock); the flash rides the shooter
            self.assertLess(x['fade_err'], 1e-3, x)
            self.assertLess(x['ride_err_px'], 0.1, x)
        for x in life.values():  # v_bullet - (slip + node velocity): the shot's own direction
            self.assertLess(x['axis_err_deg'], 0.05)
        pulse = self.record['metrics']['pulse']
        self.assertEqual(len(pulse), 4)
        for x in pulse:
            self.assertEqual(x['sky_lit'], 0)
            self.assertLess(x['worst'], 0.02)
            self.assertLessEqual(abs(x['peak'] / x['peak_want'] - 1.0), 0.05)
        cap = self.record['metrics']['pulse_cap'][0]  # the screen cap, the eye within 1.5 radii, a culled flash
        self.assertEqual((cap['own27_clamped'], cap['own27_drawn'], cap['cockpit3_flash'], cap['cockpit3_pulse'],
                          cap['offscreen_flash'], cap['offscreen_pulse']), (1, 1, 1, 0, 0, 0))
        self.assertLessEqual(cap['own27_px'], cap['cap_px'] + 0.01)
        ghost = self.record['metrics']['pulse_ghost'][0]  # in the TAA history at most one extra frame
        self.assertEqual(ghost['ran'], 1)
        self.assertLessEqual(ghost['ghost2'], 0.01)
        budget = next(x for x in rows if x.get('case') == 'budget')
        self.assertEqual((budget['flashes'], budget['pulses'], budget['lost'], budget['stale']), (64, 64, 0, 0))
        self.assertLess(budget['cpu_us'], 20.0)
        self.assertLess(budget['gpu_increment_us'], 30.0)
        combined = next(x for x in rows if x.get('case') == 'budget_combined' and x.get('tune') == 'baked')
        self.assertEqual((combined['bolts'], combined['drawn'], combined['flashes'], combined['pulses']), (512, 512, 64, 64))
        bounds = next(x for x in rows if x.get('case') == 'budget_combined_bounds')
        self.assertLessEqual(bounds['gpu_us'], 60.0)
        self.assertLessEqual(bounds['flash_work_cpu_us'], 20.0)
        self.assertLessEqual(bounds['cpu_us'], 150.0)  # a regression tripwire; the flight's CPU is projectile_us_max
        self.assertEqual(self.record['failed_checks'], [])

    def test_passed_with_the_step_gates(self):
        r = self.record
        self.assertTrue(r['passed'])
        self.assertFalse(r['game_launched'])
        self.assertEqual(r['host']['name'], 'native-windows')
        self.assertEqual(r['failed_checks'], [])
        m = r['metrics']
        self.assertEqual(sorted(x['speed_px'] for x in m['resolve']), [0, 4, 8])
        self.assertTrue(all(x['core'] >= 0.95 for x in m['resolve']))
        # 5.1.1 per tier: the footprint's cell count, the lit area bounded by hexagon edges (past the outermost cell
        # centres, inside their circumradius; 1.5 px of slack), nothing outside, lit on both sides in both axes
        tiers = {x['tier']: x for x in m['patch']}
        self.assertEqual(sorted(tiers), ['capital', 'corvette', 'fighter'])
        for x in tiers.values():
            self.assertEqual(x['cells'], x['expected_cells'])
            self.assertEqual(x['cells'], 1 + 3 * x['rings'] * (x['rings'] + 1))
            self.assertLessEqual(x['lit_px'], x['bound_px'] + 1.5)
            self.assertGreater(x['lit_px'], x['reach_px'])
            self.assertEqual(x['outside_sum'], 0.0)
        self.assertEqual(sorted(x['tier'] for x in m['symmetry']), ['capital', 'corvette', 'fighter'])
        for x in m['symmetry']:
            sides = [x['left'], x['right'], x['up'], x['down']]
            self.assertGreater(min(sides), 0, x['tier'])
            self.assertGreaterEqual(min(sides), 0.8 * max(sides), x['tier'])
        # the cell-map model: a second adjacent hit resets only the shared cells, no cell drawn twice, the grid fixed
        # on a moved owner, no pop at 12 hits per second
        adj = m['adjacent'][0]
        self.assertEqual(adj['cells'], 10)
        self.assertLessEqual(adj['peak_pair'], adj['peak_single'] * 1.002)
        self.assertAlmostEqual(adj['pair_m1'], adj['first_m1'], delta=2e-3 * max(1.0, adj['first_m1']))
        self.assertAlmostEqual(adj['pair_0'], adj['second_0'], delta=2e-3 * max(1.0, adj['second_0']))
        self.assertEqual(m['grid'][0]['identical'], 1)
        nopop = {x['case']: x for x in m['nopop']}
        for case in ('fighter_12hps', 'capital_spread_12hps'):
            self.assertLessEqual(nopop[case]['excess'], 0.0, case)
            self.assertEqual(nopop[case]['popped_pixels'], 0, case)
            self.assertGreaterEqual(nopop[case]['max_live_hits'], 30, case)
            self.assertEqual(nopop[case]['cells_dropped'], 0, case)
        cap = nopop['capital_spread_12hps']
        self.assertGreater(cap['max_region_cells'], 128)                          # the map overflowed: spill draws
        self.assertGreater(cap['spills'], 0)
        witness = nopop['capital_spread_one_draw_witness']                         # one draw per region: evictions
        self.assertGreater(witness['cells_dropped'], 0)
        self.assertIn('popped_pixels', witness)
        centre = m['centre'][0]                                                    # the marker cell, no core flash
        self.assertGreater(centre['ratio'], 1.5)
        self.assertAlmostEqual(centre['ratio'], centre['expected'], delta=0.02 * centre['expected'])
        seam = m['seam'][0]                                                       # the faces share, never add
        self.assertGreaterEqual(seam['pair_draws'], seam['single_draws'])
        self.assertLessEqual(seam['peak_pair'], max(seam['peak_a'], seam['peak_b']) * 1.002 + 1e-3)
        self.assertEqual(sorted(x['case'] for x in m['stage_cpu']),
                         ['one_capital_64_hits', 'sixteen_capitals_16_hits', 'station_flight_b_cluster_64_hits'])
        # The ship bubble (user decision after Run238 Flight E): every ship case on its bubble from the hull walk, the lit
        # near side's centroid within a cell of the hit's bubble point, nothing the twin hides, nothing past the
        # footprint (not on the tail), the bubble near the hit covered, the lit count of the same order for every
        # approach direction; the station keeps the hull decal (nothing on sky); dir unused (the 42-degree bolt)
        place = {x['case']: x for x in m['placement']}
        for case in ('narrow_mid_flank', 'aft_flank', 'y_top_narrow', 'y_bottom_narrow', 'flat_flank', 'y_top_flat',
                     'y_bottom_flat', 'y_top_flat_42deg', 'station_flight_b_0p68R',
                     'y_top_flat_offcentre'):  # review F1: the hull off the owner's origin lights
            x = place[case]
            station = case.startswith('station')
            self.assertEqual((x['surface'], x['bubbles']), ('hull', 0) if station else ('bubble', 1), case)
            if station:
                self.assertEqual((x['sky_lit'], x['outside_lit']), (0, 0), case)
            self.assertLessEqual(x['off_bubble_lit'] * 200, x['lit_pixels'], case)
            self.assertLessEqual(x['outside_lit'] * 100, x['lit_pixels'], case)
            self.assertGreaterEqual(x['coverage'], 0.9, case)
            self.assertLessEqual(x['centroid_error_cells'], 1.0, case)
            self.assertGreaterEqual(x['lit_ratio'], 1.0 / 3.0, case)
            self.assertLessEqual(x['lit_ratio'], 3.0, case)
        self.assertEqual(place['y_top_flat_42deg']['lit_pixels'], place['y_top_flat']['lit_pixels'])  # dir unused
        # The bubble's sides: the chase camera's nose hit draws on the far side around the outline, the hull hiding most
        # of it (the hidden share measured), nothing the twin hides; the eye inside the bubble draws the far side only, the
        # default image at bubble_far_gain of it
        sides = {(x['case'], x['side']): x for x in m['bubble_side']}
        chase = sides[('chase_nose_from_behind', 'far')]                       # review F3: hundreds of far pixels,
        self.assertEqual(sides[('chase_nose_from_behind', 'near')]['lit_pixels'], 0)  # the near side unlit
        self.assertGreaterEqual(chase['lit_pixels'], 500)
        self.assertGreaterEqual(sides[('chase_nose_from_behind', 'both')]['cap_nose_m'], 1.2)  # the cap past the nose
        aft = place['aft_flank']                                                  # 1.15 x radius: the cap stays
        self.assertGreaterEqual((aft['cap_nose_m'], aft['cap_tail_m']), (5.0, 5.0))
        self.assertGreater(chase['hidden_share'], 0.0)
        self.assertLessEqual(chase['hidden_lit'] * 200, chase['lit_pixels'])
        self.assertLessEqual(chase['outside_lit'] * 100, chase['lit_pixels'])
        ap = m['axis_plane'][0]                                                   # review F2: no flip, no double light
        self.assertEqual((ap['draws_plus'], ap['draws_minus']), (ap['draws_zero'], ap['draws_zero']))
        for k in ('lit', 'peak', 'sum'):
            for side in ('plus', 'minus'):
                self.assertAlmostEqual(ap['%s_%s' % (k, side)], ap['%s_zero' % k], delta=2e-3 * max(1.0, ap['%s_zero' % k]))
        self.assertEqual(sides[('eye_inside_bubble', 'near')]['lit_pixels'], 0)
        inside = sides[('eye_inside_bubble', 'far')]
        self.assertGreater(inside['lit_pixels'], 100)
        self.assertGreaterEqual(inside['coverage'], 0.9)
        self.assertAlmostEqual(sides[('eye_inside_bubble', 'both')]['ratio'], 0.6, delta=0.01)
        walk = m['hull_walk'][0]                                                 # 127 parts within the budget, cached
        self.assertEqual((walk['ok'], walk['parts'], walk['nodes'], walk['bubble_fallback']), (1, 127, 127, 0))
        self.assertEqual(walk['engine_memory_ok'], 1)                             # review F4: the production reader
        self.assertGreater(walk['reads'], 127)
        self.assertGreaterEqual(walk['cache_hits'], 199)
        normals = {x['case']: x for x in m['normals']}                             # ddx / ddy against a neighbourhood
        for x in normals.values():
            self.assertLess(x['ddx_mean_deg'], 1.0)
            self.assertLessEqual(x['ddx_wrong_axis'] - x['ddx_wrong_axis_silhouette'],
                                 2 * (x['neighbour_wrong_axis'] - x['neighbour_wrong_axis_silhouette']) + 50)
        cull = {x['case']: x for x in m['cull']}                                  # the face cull at its cut
        self.assertEqual((cull['away']['culled'] >= 1, cull['away']['has_z']), (True, 0))
        self.assertLess(cull['away']['dot_max'], cull['across']['cut'])
        self.assertLess(cull['across']['dot_min'], cull['across']['cut'])
        self.assertGreater(cull['across']['dot_max'], cull['across']['cut'])
        self.assertEqual(cull['across']['has_z'], 1)
        surf = m['surface'][0]                                                    # the surface is the lane
        self.assertEqual((surf['sky'], surf['past'], surf['half_right_lit']), (0.0, 0.0, 0))
        self.assertGreater(surf['nearer'], 0.0)
        self.assertLess(m['attach_slots'][0]['shell_ps'], 2000)                   # the low thousands
        self.assertLess(m['attach_slots'][0]['bubble_ps'], 2000)
        # Flight C: the own ship in the cockpit view at own_cockpit_gain; the chase view and another ship unchanged
        ck = {x['case']: x for x in m['cockpit']}
        self.assertEqual((ck['own_chase']['dimmed'], ck['own_cockpit']['dimmed'], ck['other_cockpit']['dimmed'],
                          ck['unknown_own_cockpit']['dimmed']), (0, ck['own_cockpit']['patches'], 0, 0))
        self.assertGreaterEqual(ck['own_cockpit']['patches'], 1)
        self.assertAlmostEqual(ck['summary']['own_cockpit_ratio'], ck['summary']['gain'], delta=0.01)
        self.assertEqual((ck['summary']['other_ratio'], ck['summary']['unknown_ratio']), (1.0, 1.0))
        self.assertEqual((ck['own_cockpit_stale']['dimmed'], ck['summary']['stale_ratio']), (0, 1.0))  # review F2
        sort = {x['case']: x for x in m['sort']}                                     # review F5: the heap sort
        self.assertLess(sort['shuffled']['heap_us'], 5.0)
        for case in ('fighter_sphere_front_12hps', 'capital_sphere_front_12hps', 'station_flat_12hps'):  # no pop
            self.assertEqual(nopop[case]['popped_pixels'], 0, case)
        fade = m['fade']
        self.assertEqual(fade['t%d' % fade['lifetime_ms']], 0.0)                 # nothing left at the record's lifetime
        age = m['age'][0]
        self.assertGreaterEqual(age['seta6_wall_ms'], age['seta6_expected_wall_ms'])  # measured, not computed
        self.assertLessEqual(age['seta6_wall_ms'], age['seta6_expected_wall_ms'] + 0.2 + age['max_frame_ms'] + 0.5)
        self.assertEqual((m['retired'][0]['retired'], m['retired'][0]['ring_live'], m['retired'][0]['drew']), (1, 0, 0))
        self.assertEqual(m['refused'][0]['reason'], 'program_create')
        cases = {(x['width'], x['case']) for x in m['timing']}
        for width in (1920, 5120):
            for case in ('fence', 'fighter_two_hits_one_region', 'fighter_two_regions',
                         'capital_close_two_hits_one_region', 'capital_close_two_regions', 'worst_four_regions',
                         'fighter_12hps_map', 'capital_close_full_map', 'capital_fill_full_map', 'capital_spread_spill',
                         'bubble_fighter_two_hits', 'bubble_capital_close_two_hits', 'bubble_worst_one_owner_four_hits',
                         'bubble_eye_inside_front_hit', 'bubble_cockpit_eight_hits'):
                self.assertIn((width, case), cases)
        fill = [x for x in m['timing'] if x['case'] == 'capital_fill_full_map']
        self.assertTrue(all(x['raster_share'] >= 0.95 for x in fill))             # its cells really fill the frame

    def test_bound_to_its_production_sources(self):
        import run_weapon_effects as runner
        source = self.record['source']
        self.assertEqual(self.record['production_sources'], list(runner.PRODUCTION_SOURCES))
        for path, recorded in source['sha256'].items():
            self.assertEqual(sha(path), recorded, path + ': changed since the record: rerun run_weapon_effects.py --host native')


if __name__ == '__main__':
    unittest.main()
