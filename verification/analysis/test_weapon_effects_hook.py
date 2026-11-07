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
                # step 2, the hull hit (docs/architecture/weapon-look.md 3.1): the shields,hull mode's skip and forward
                'hull_mode_parse', 'hull_mode_installed', 'hull_hit_skips_native_when_staged', 'hull_skip_frame_kept',
                'hull_mode_shield_hit_staged', 'hull_hit_forwards_when_not_owned', 'hull_hit_dying_object_forwards',
                'drained_hull_hit_staged', 'hull_hit_stage_and_forward', 'hull_missile_source_staged',
                'hull_asteroid_variant', 'hull_repair_tug_variant_by_override', 'hull_present_keeps_floor_then_releases',
                'hull_hit_forwards_without_hull_programs', 'overflow_census_first_then_oldest_staged',
                # the 1024-record ring (2026-11-07): the hidden owner's records first, the handler's cost when full
                'overflow_1024_hidden_owner_first_then_oldest', 'event_cost_50_full_ring_1024',
                'overflow_counts_evicted_visible'}

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
                                              '_x3m_weapon_muzzle': [], '_x3m_weapon_flight': []})
        self.assertTrue(a['stub_ok'] and a['stub_bolt_tap_ok'] and a['stub_muzzle_ok'] and a['stub_flight_ok'])
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


# The lightning's bounds (us; 2026-11-07, set from the native measurement in docs/verification/weapon-effects.md "Beam
# colour and ion lightning"): 64 lit ion hits on a capital (CPU, best of 200), the same 64 at close range (GPU, 5120x1440,
# EVENT-fenced best of 60), and the 64 mixed hits' CPU bound. The close GPU bound 450 -> 600 at the burst look's merge
# (2026-11-07: the ion's flash a star, its 64 stars 3.30 charged screens against the discs' 2.77; 497.8 measured against
# 370.3; docs/verification/weapon-effects.md "Burst review fixes and the merge").
ION_CPU_BOUND_US, ION_CLOSE_GPU_BOUND_US, MIXED_CPU_BOUND_US = 35.0, 600.0, 50.0


class WeaponEffectsD3DRecord(unittest.TestCase):
    PATH = RESULTS / 'summary.json'

    def setUp(self):
        self.record = json.loads(self.PATH.read_text())

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

    def test_hull_hit_gates(self):
        """Step 2 (docs/architecture/weapon-look.md 3.1 "Fixture gates"): every hull gate passed, and the rows behind
        them: placement, the hidden half, the floors, the three-frame floor, no pop, the cockpit dim, the resolve, the CPU
        and GPU budgets at 5120x1440, and the glow's budget weight re-pinned from its full-screen cost."""
        r = self.record
        names = {n for n, ok in r['checks'] if ok}
        for gate in ('hull_programs_attached', 'hull_sparks_born_at_recorded_pos', 'hull_flash_centroid_at_projected_hit',
                     'hull_half_of_sparks_hidden_by_flat_lane', 'hull_light_on_hull_not_sky', 'hull_light_peak_decay_burst_law',
                     'hull_spark_cooling_hue_half_life', 'hull_spark_cooling_ramp', 'hull_sizes_counts_per_tier_law',
                     'hull_spark_reach_law_100ms', 'hull_spark_reach_asymptote_energy_1_35u', 'hull_spark_lives_law',
                     'hull_streak_min_footprint_3px_24px', 'hull_min_frames_under_seta6', 'hull_min_frames_under_seta10',
                     'hull_seta6_wall_time_law', 'hull_no_pop_12hps', 'hull_far_side_hit_hidden',
                     'hull_flash_not_halved_by_its_surface', 'hull_chase_nose_lit_outside_outline', 'hull_cockpit_dim_own_ship',
                     'hull_repair_no_sparks', 'hull_tug_glow_only', 'hull_missile_tier', 'hull_asteroid_dust',
                     'velocity_from_frame_delta', 'hull_orphan_sparks_outlive_owner', 'hull_budget_shed',
                     'hull_r32f_lane_form', 'hull_resolve_core_survival_0px', 'hull_resolve_core_survival_4px',
                     'hull_resolve_core_survival_8px', 'hull_resolve_no_scar_0px', 'hull_resolve_no_scar_4px',
                     'hull_resolve_no_scar_8px', 'hull_fault_reported', 'hull_floor_latched_seta_engage_nothing_reappears',
                     'hull_no_pop_step_change_law', 'hull_programs_refused_shields_still_draw',
                     'shields_mode_loads_no_hull_programs', 'velocity_first_hit_from_previous_frame_rows',
                     'hull_recovers_after_fault', 'hull_reset_recovers', 'hull_program_refusal_draws_nothing',
                     'hull_cpu_64_hits', 'hull_budget_20_hits_5120',
                     # 2026-11-06: the burst, the randomised sparks, the heavy ring, the eye sphere
                     'hull_burst_radius_law_3_ages', 'hull_burst_colour_white_core_at_birth',
                     'hull_burst_colour_dark_red_at_end', 'hull_burst_half_hidden_by_lane', 'hull_spark_random_hits_differ',
                     'hull_spark_same_hit_stable', 'hull_spark_curl_share', 'hull_resolve_burst_survival',
                     'hull_resolve_burst_no_scar',
                     'hull_budget_burst_lod_then_shed', 'hull_fireball_fault_reported_and_recovers',
                     'cockpit_eye_sphere_hits_light_their_half', 'cockpit_eye_sphere_centroid_within_one_cell',
                     'cockpit_eye_sphere_chase_unchanged',
                     # the hull-impact classes (docs/architecture/hull-impact-classes.md section 8)
                     'hull_class_programs_attached', 'hull_class_anatomy_round_trip', 'hull_scorch_multiplies_hull_only',
                     'hull_scorch_store_outlives_record', 'hull_scorch_through_temporal_0px',
                     'hull_scorch_through_temporal_8px', 'hull_classes_distinct_greyscale', 'hull_energy_puff_grows_cools',
                     'hull_energy_spatter_4_4k', 'hull_energy_ember_outlasts_flash_10x',
                     'hull_ballistic_two_spark_lifetimes', 'hull_ballistic_splash_back_bias',
                     'hull_ballistic_chips_keep_velocity', 'hull_ballistic_no_fireball', 'hull_plasma_bloom_2x_energy',
                     'hull_plasma_globe_shrinks_reddens', 'hull_plasma_ring_on_surface_not_sky',
                     'hull_heavy_entry_streak_along_dir', 'hull_heavy_back_jet_cone',
                     'hull_heavy_exit_jet_emerges_thin_hull', 'hull_heavy_shimmer_rect_submitted',
                     'hull_beam_hold_relights_not_appends', 'hull_beam_sweep_trail_without_strobe',
                     'hull_beam_pulse_flash_after_gap', 'hull_beam_spot_weapon_colour', 'hull_beam_ember_scorch_stay_heat',
                     'hull_beam_white_keeps_heat', 'hull_beam_stock_pbc_orange_mix',
                     'hull_ion_lightning_law_16_seeds', 'hull_ion_lightning_on_surface_restrikes',
                     'hull_ion_lightning_within_filament_charge', 'hull_ion_core_floor_corona_4x', 'hull_ion_no_heat_post_hit',
                     'hull_ion_plane_on_narrow_hull', 'hull_cpu_64_hits_ion', 'hull_ion_cap_cuts_whole_hits_oldest',
                     'hull_ion_shed_whole_channels_oldest',
                     'hull_missile_debris_two_sets_and_ring', 'hull_repair_relit_per_tick',
                     'hull_asteroid_no_glow_no_scorch_dim_puff', 'hull_compose_no_pop_per_class',
                     'hull_min_frames_under_seta6_per_class', 'hull_class_faults_reported_and_recover',
                     'hull_cpu_64_hits_mixed', 'hull_budget_capital_close_within_8_screens', 'hull_budget_capital_close_64_within_budget',
                     # the 1024-record ring (2026-11-07): the stamp, the CPU bounds, the shed order past 256 hits
                     'hull_ring_1024_visible_64_stamp', 'hull_ring_1024_visible_256_stamp', 'hull_cpu_ring_1024_visible_64',
                     'hull_cpu_ring_1024_visible_256', 'hull_ring_1024_visible_256_shed_within_budget',
                     'hull_visible_first_keeps_all_on_screen', 'hull_reach_edge_sparks_unchanged',
                     'hull_reach_beyond_margin_builds_nothing',
                     # review 2026-11-07: the heavy class's sweep with its streak, the moving owner's spark sphere, each
                     # owner past a cap read once a frame for both stages
                     'hull_reach_heavy_edge_unchanged', 'hull_reach_heavy_beyond_margin_builds_nothing',
                     'hull_reach_moving_owner_sparks_kept', 'stage_owners_512_located_once',
                     'stage_owners_1024_located_once',
                     # the burst look (docs/architecture/hull-burst-look.md section 8 and the addendum)
                     'hull_burst_contrast', 'hull_burst_silhouette_lobed', 'hull_burst_erodes_outside_in',
                     'hull_burst_colour_core_rim', 'hull_glow_follows_burst', 'hull_20_hits_no_white_sun',
                     'hull_flash_star_3_frames', 'hull_smoke_darkens_hull_only', 'hull_burst_small_has_structure',
                     'hull_asteroid_dust_readable', 'hull_repair_cool_spot',
                     # the burst look's review (2026-11-07): the octave blend and its hysteresis, the cluster's fade, the
                     # ring classes' annulus, the ember's lobes
                     'hull_burst_octave_blend_no_jump', 'hull_budget_octave_hysteresis', 'hull_cluster_neighbour_fades',
                     'hull_ring_class_no_dark_annulus', 'hull_ember_lobed_remnant', 'hull_ember_lobed_within_glow_weight') + tuple(
                         'hull_budget_20_hits_5120_' + c for c in ('energy', 'ballistic', 'plasma', 'heavy', 'ion', 'beam',
                                                                   'missile', 'asteroid')):
            self.assertIn(gate, names)
        m = r['metrics']
        hull = {x['case']: x for x in m['hull'] if 'case' in x and x['case'] != 'law'}
        self.assertLessEqual(hull['placement']['spark_centroid_err_u'], 0.25)
        self.assertLessEqual(hull['placement']['flash_centroid_err_rhit'], 0.1)
        for k in ('k0_share', 'k05_share', 'k1_share'):
            self.assertTrue(0.45 <= hull['half_hidden'][k] <= 0.55, k)
        self.assertEqual(hull['glow_surface']['lit_sky'], 0)
        self.assertGreaterEqual((hull['floors_2km']['min_width_px'], hull['floors_2km']['min_length_px']), (3.0 - 1e-3, 12.0 - 1e-2))
        self.assertEqual((hull['min_frames']['seta6_flash_frames'], hull['min_frames']['seta10_flash_frames']), (3, 3))
        self.assertEqual(hull['no_pop_12hps']['popped_pixels'], 0)
        self.assertEqual((hull['no_pop_step_change']['violations'], hull['no_pop_step_change']['checked']), (0, 720))
        self.assertEqual(hull['seta_engage']['reappeared'], 0)
        self.assertEqual(hull['chase_nose']['age_ms'], 200)
        self.assertGreaterEqual(hull['chase_nose']['energy_outside_lit'], 20)
        self.assertGreaterEqual(hull['chase_nose']['ballistic_outside_lit'], 20)
        self.assertEqual((hull['hull_refused']['hull'], hull['hull_refused']['patches'], hull['hull_refused']['sprites']), (0, 1, 0))
        self.assertAlmostEqual(hull['cockpit']['own_ratio'], 0.35, delta=0.005)
        laws = [x for x in m['hull'] if x.get('case') == 'law']
        self.assertEqual(len(laws), 9)                                            # 3 tiers x k 0 / 0.5 / 1
        for x in laws:
            self.assertEqual(x['sparks'], x['want'])
        cpu = {x['case']: x for x in m['hull_cpu']}
        self.assertLessEqual(cpu['capital_64_hits']['best_us'], 40.0)    # 30 before the burst and the ring (2026-11-06)
        # the beam colour and ion lightning review (2026-11-07): the lightning's CPU and GPU bounds from the measurement
        # (docs/verification/weapon-effects.md "Beam colour and ion lightning"), its cap and shed, the narrow hull's plane
        ccpu = {x['case']: x for x in m['class_cpu']}
        self.assertLessEqual(ccpu['capital_64_ion']['best_us'], ION_CPU_BOUND_US)
        self.assertGreater(ccpu['capital_64_ion']['capped'], 0)
        self.assertLessEqual(ccpu['capital_64_mixed']['best_us'], MIXED_CPU_BOUND_US)
        rows = [x for x in m['class'] if x.get('case') == 'ion_plane' and x.get('lane_normal') == 1]
        self.assertEqual(len(rows), 3)
        for x in rows:
            self.assertEqual(x['sky'], 0, x['hit'])
            self.assertLess(x['peak_over_straight'], 2.0, x['hit'])
            self.assertTrue(0.5 <= x['channel_over_straight'] <= 1.5, x['hit'])
        shed = next(x for x in m['class'] if x.get('case') == 'ion_cap_shed')
        self.assertGreater(shed['capped'], 0)
        self.assertGreater(shed['shed'], 0)
        self.assertEqual(shed['whole_channels'], 1)
        slots = m['class_slots'][0]
        self.assertGreater(slots['lightning_vs'], 0)
        self.assertEqual(slots['lightning_vertex_bytes'], 64)
        ion_close = {x['width']: x for x in m['class_timing'] if x.get('class') == 'ion' and
                     x.get('case') == 'capital_close_64_hits'}
        self.assertLessEqual(ion_close[5120]['gpu_us'], ION_CLOSE_GPU_BOUND_US)
        for case, hidden in (('ring_1024_visible_64', 960), ('ring_1024_visible_256', 768)):  # the full 1024-record ring
            self.assertEqual((cpu[case]['live'], cpu[case]['hidden']), (1024, hidden), case)
            self.assertLessEqual(cpu[case]['best_us'], cpu[case]['target_us'], case)
        timing = {(x['width'], x['case']): x for x in m['hull_timing'] if 'width' in x}
        self.assertLessEqual(timing[(5120, 'fighter_200m_20_hits')]['gpu_us'], 80.0)  # 50 before the bursts, 70 before the burst look
        # the glow's budget weight (core::look::budget_weight_glow) against its measured full-screen cost / 177.5 us
        core_h = (ROOT / 'src/proxy/weapon_effects_core.h').read_text()
        import re
        # the classes' primitives (hull::budget_weight_ring, _filament, _scorch, _flash), the same law
        classes = {x['case']: x for x in m['class_timing'] if x.get('width') == 5120 and 'weight' in x}
        for name, case in (('budget_weight_ring', 'ring_alone_fills_frame'), ('budget_weight_scorch', 'scorch_alone_fills_frame'),
                           ('budget_weight_flash', 'flash_alone_fills_frame')):
            weight = float(re.search(r'\b%s = ([0-9.]+)f' % name, core_h).group(1))
            self.assertAlmostEqual(weight, classes[case]['gpu_us'] / 177.5, delta=0.1, msg=name)
        # the lightning program on every pixel (2026-11-07): the filament glow's weight 0.86 stays its bound
        weight = float(re.search(r'\bbudget_weight_filament = ([0-9.]+)f', core_h).group(1))
        self.assertLessEqual(classes['filament_alone_fills_frame']['gpu_us'] / 177.5, weight + 0.1)
        weight = float(re.search(r'budget_weight_glow = ([0-9.]+)f', core_h).group(1))
        measured = timing[(5120, 'glow_alone_fills_frame')]['gpu_us'] / 177.5
        self.assertAlmostEqual(weight, measured, delta=0.1)
        # the fireball's (the burst's) at three octaves and at two (the budget's step 1), and as smoke: charged at the
        # worst case (every disc pixel taking the noise), never below the alone rows (the lobes skip their outside there)
        for name, case in (('budget_weight_fireball', 'burst_alone_fills_frame'),
                           ('budget_weight_fireball_coarse', 'burst_alone_fills_frame_coarse'),
                           ('budget_weight_smoke', 'smoke_alone_fills_frame')):
            weight = float(re.search(r'\b%s = ([0-9.]+)f' % name, core_h).group(1))
            self.assertGreaterEqual(weight, timing[(5120, case)]['gpu_us'] / 177.5 - 0.1, msg=name)
            # review 2026-11-07: the worst case itself is a row (every pixel taking the noise), the weight its cost
            worst = timing[(5120, case.replace('fills_frame', 'every_pixel_noise'))]['gpu_us'] / 177.5
            self.assertAlmostEqual(weight, worst, delta=0.15, msg=name)
        # the dense frame within the budget's 8 x 177.5 us
        self.assertLessEqual(timing[(5120, 'capital_close_64_hits')]['gpu_us'], 1420.0)

    def test_lightning_log_fields(self):
        """Review 2026-11-07: the device row logs the lightning's programs (filament_ps_slots, lightning_vs_slots, the
        lane fetch per format) with the others, the F8 hull frame row its segments (ion_segments)."""
        text = (ROOT / 'src/proxy/motion_output_weapon_effects_inc.h').read_text()
        device = text[text.index('log("weapon_effects_device device=%llu frame=%llu attached=%u reason=%s result='):]
        device = device[:device.index(');')]
        for field in ('filament_ps_slots=%u', 'lightning_vs_slots=%u', 'lightning_vtf=%u,%u'):
            self.assertIn(field, device)
        self.assertIn('c.lightning_vs_slots', device)
        frame = text[text.index('log("weapon_hull_frame'):]
        frame = frame[:frame.index(');')]
        self.assertIn('ion_segments=%u', frame)
        self.assertTrue(frame.rstrip().endswith('hn.ion_segments'))

    def test_bound_to_its_production_sources(self):
        import run_weapon_effects as runner
        source = self.record['source']
        self.assertEqual(self.record['production_sources'], list(runner.PRODUCTION_SOURCES))
        for path, recorded in source['sha256'].items():
            self.assertEqual(sha(path), recorded, path + ': changed since the record: rerun run_weapon_effects.py --host native')


if __name__ == '__main__':
    unittest.main()
