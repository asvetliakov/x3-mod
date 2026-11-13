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
                # the reshade mode's bullet batch scope site 0x0046d0a7 (weapon_effects_batch_site.h)
                'reshade_installs_impact_and_batch_scope_only', 'reshade_scope_stub_forwards_and_scopes_the_submit',
                'reshade_shutdown_disarms', 'shields_leaves_batch_site', 'reshade_batch_every_window_byte_refused',
                'reshade_batch_rollback_restores_all', 'reshade_mode_rows', 'reshade_late_claim_refused',
                # step 3, the death sites (reached through install_at)
                'death_decision_seam', 'death_installed', 'death_forwards_when_not_owned',
                'death_skips_fighter_when_owned', 'death_skips_missile_when_owned', 'death_forwards_big_tier_census',
                'death_forwards_asteroid', 'death_forwards_rule_b_missile', 'death_area_bullet_forwards',
                'death_retires_owner_hits', 'death_nested_stack', 'death_present_keeps_ours_releases_census',
                'death_forwards_without_boom_programs', 'death_shutdown_restores_both',
                'death_mode_without_token_leaves_sites', 'death_byte_mismatch_refused', 'death_rollback_restores_both',
                'death_nested_after_return',
                # the surface at the hook (user decision 2026-11-12 on FL 69 A: the walk removed): a ship with a profile
                # staged, without a usable one forwarded native and counted per verdict, a station staged
                'route_fighter_profile_staged', 'route_fighter_no_profile_native', 'route_capital_no_profile_native',
                'route_class_mismatch_native', 'route_stale_table_native', 'route_no_profiles_loaded_native',
                'route_axis_scale_native', 'route_station_staged', 'route_other_object_native',
                'route_native_rows_listed_once',
                # review F1 / F2: the native rows across the session, the install path's own profiles pointer
                'route_native_rows_session_first_seen_then_more', 'route_install_wires_null_profiles_native',
                'route_reinstall_shields'}

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
        # the reshade mode's batch scope stub: its shape (the body id [[esp+8]+0x0c], the four re-pushed words under the
        # 12-byte pad, the plain ret) and its window, the installed EXE's bytes
        self.assertTrue(a['stub_batch_scope_ok'] and a['batch_window_exe'])
        self.assertGreaterEqual(a['engine_patch_lock_cmpxchg8b'], 1)
        # the stub (0x20 + the return address) and the handler's frame stayed inside the fixture's margin; the deepest
        # written byte lies in the frame's lowest dword (a sub-dword store there reads up to 3 bytes shallower; measured
        # 2026-11-13 with the native rows' bitmap: depth 237 against 240 for the 204-byte frame)
        depth, frame = self.record['stack_depth']['bytes'], a['handler_frame_bytes'] + 0x24
        self.assertTrue(frame - 4 < depth <= frame, (depth, frame))

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
                'object_tap_handler_records_ecx_object', 'muzzle_forwards_registers_stack_flags',
                'flight_forward_reaches_callee_bullet', 'flight_skip_no_callee',
                'death_forward_records_other_tag_reason_killer', 'death_skip_no_callee_eax_zero',
                'fl_node_body_offset', 'fixture_stack_margin',
                # the bullet batch submit: claimed only in reshade mode, with the scope stub (bullet-draw-batch.md
                # section 6); shields mode claims the impact alone; the bolts modes' muzzle witness and rollback; the
                # removed projectile stage logs nothing
                'reshade_batch_rollback_restores_all', 'reshade_installs_impact_and_batch_scope_only',
                'reshade_scope_stub_forwards_and_scopes_the_submit', 'reshade_shutdown_disarms',
                'shields_mode_claims_impact_only', 'shields_mode_shutdown_restores', 'bolts_mode_muzzle_witness_refused',
                'bolts_mode_muzzle_rollback_restores_all', 'init_logs_no_projectile_rows'}

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
        self.assertTrue(all(a['stub_ok'].values()) and len(a['stub_ok']) == 7)  # six shims and the batch's re-shade scope
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

    def test_reshade_texture_key(self):
        # bullet_reshade_core.h (bullet-draw-batch.md section 6): the exact bytes find their entry, a changed tail or
        # head, another size or no table none; fx_bullets_diff's p99 keeps 2.75, fx_bullets2_diff's scales by
        # 0.6169 / 0.9888; the observer's match is cheap on a size miss
        checks = dict(self.record['checks'])
        for name in ('reshade_texture_key_matches_exact_bytes', 'reshade_texture_scale_per_texture',
                     'reshade_geometric_matches_model', 'reshade_geometric_keys_at_one_unchanged'):
            self.assertTrue(checks.get(name), name)
        # the geometric size (projectile-look.md "Geometric size (2026-11-10)"): the falloff's readback on the model at
        # rs_head 0.5 / rs_width 0.6, the keys at 1 bit for bit the layout-free program, the body change's CPU cheap
        geo = next(x for x in self.record['metrics']['reshade'] if x.get('case') == 'geometric_model')
        self.assertLessEqual(geo['worst_rel'], 1.0)
        self.assertEqual(geo['same_at_one'], 1)
        cost = next(x for x in self.record['metrics']['reshade'] if x.get('case') == 'layout_cost')
        self.assertLess(cost['layout_us_per_body_change'], 0.2)
        row = next(x for x in self.record['metrics']['reshade'] if x.get('case') == 'texture_key')
        self.assertEqual((row['hit'], row['tail_miss'], row['head_miss'], row['size_miss'], row['no_table']),
                         (0, -1, -1, -1, -1))
        self.assertAlmostEqual(row['scale_ire'], 2.75, places=2)
        self.assertAlmostEqual(row['scale_ppc'], 2.75 * 0.6169 / 0.9888, places=2)
        self.assertLess(row['us_size_miss'], 1.0)

    def test_reshade_lone_path(self):
        # bullet-draw-batch.md section 7: the lone draw is recognised by the scope node's body id (the effects VS and
        # blend as the ABI gate), our lone program reads the fade from COLOR0.x, and the route's sequence converts,
        # flags and restores it; the recognition is cheap for a hit and for a miss (every other effects draw)
        checks = dict(self.record['checks'])
        for name in ('reshade_lone_recognised_by_body_id', 'reshade_lone_programs_create',
                     'reshade_lone_program_matches_model', 'reshade_lone_route_binds_converts_flags_restores',
                     'reshade_lone_state_admitted_normalised_restored', 'reshade_lone_state_verdict',
                     'reshade_lone_native_flag_one_one', 'reshade_lone_flag_invsrccolor_witness',
                     'reshade_lone_cost_measured'):
            self.assertTrue(checks.get(name), name)
        rows = self.record['metrics']['reshade']
        self.assertTrue(all(x['ok'] == 1 for x in rows if x.get('case') == 'lone_recognition'))
        route = next(x for x in rows if x.get('case') == 'lone_additive')
        self.assertEqual((route['during'], route['restored']), (1, 1))
        # FL 68 A: the lone draw's own state (every normalised bit off) admitted as a lone draw, refused as any other,
        # the five states the batch bolt's while drawing and the lone draw's own after
        self.assertEqual((route['verdict'], route['states_normalised_and_restored']), (1, 1))
        self.assertEqual(next(x for x in rows if x.get('case') == 'lone_state_verdict')['ok'], 1)
        # the far flag outside the route: ONE / ONE flags over the far-depth lane (g > 1) and restores; ONE /
        # INVSRCCOLOR would leave g at the lane's 1 (D3D9's one blend state for every target), so it is refused
        native = next(x for x in rows if x.get('case') == 'lone_native_flag')
        self.assertGreater(native['rt2_g'], 1.0)
        self.assertEqual((native['during'], native['restored']), (1, 1))
        witness = next(x for x in rows if x.get('case') == 'lone_flag_invsrccolor_witness')
        self.assertEqual((witness['flag_test'], witness['refused']), (0, 1))
        cost = next(x for x in rows if x.get('case') == 'lone_cost')
        self.assertLess(max(cost['match_us_hit_30'], cost['match_us_miss_30']), 1.0)
        self.assertLess(cost['normalise_us_per_draw'], 1.0)

    def test_reshade_lone_classic_path(self):
        # bullet-draw-batch.md section 7 "Classic bodies": a classic bullet body drawn alone (the standard_lighting
        # DEFAULT pair, the game's own program bytes from the FL archive) is recognised by its body id under the lone
        # blend law with one of the two pixel programs, re-shaded by the ps_3_0 classic program (COLOR0.a the fade, the UV
        # after g_TexMatrix) equal to the batch frame at three depths, far-flagged under ONE / ONE, restored; the
        # refusals stay vanilla bit for bit; the per-draw match is cheap for a hit and a miss
        checks = dict(self.record['checks'])
        for name in ('reshade_lone_classic_programs_create', 'reshade_lone_classic_recognised_by_body_id',
                     'reshade_lone_classic_targets',
                     'reshade_lone_classic_program_matches_model', 'reshade_lone_classic_matches_batch_at_three_depths',
                     'reshade_lone_classic_sequence_restores', 'reshade_lone_classic_native_flag_one_one',
                     'reshade_lone_classic_refusals_stay_vanilla', 'reshade_lone_classic_cost_measured'):
            self.assertTrue(checks.get(name), name)
        programs = self.record['classic_programs']['programs']
        self.assertEqual({k: v['hash'] for k, v in programs.items()},
                         {'classic_vs.bin': '494fe349b8bc12ec', 'classic_ps.bin': 'ced7edef48059e84',
                          'classic_ps2s.bin': '0d5848c8489b6630'})
        rows = self.record['metrics']['reshade']
        game = next(x for x in rows if x.get('case') == 'lone_classic_programs')
        self.assertEqual((game['loaded'], game['game_hashes'], game['ps_major']), (1, 1, 3))
        self.assertTrue(all(x['ok'] == 1 for x in rows if x.get('case') == 'lone_classic_recognition'))
        depths = [x for x in rows if x.get('case') == 'lone_classic_depth']
        self.assertEqual(sorted(x['distance_m'] for x in depths), [50, 300, 1500])
        for x in depths:
            self.assertEqual(x['px_off_batch'], 0)
            self.assertGreater(x['reshade_lit_px'], 0)
        cost = next(x for x in rows if x.get('case') == 'lone_classic_cost')
        self.assertLess(max(cost['match_us_hit_30'], cost['match_us_miss_30']), 1.0)
        self.assertLess(cost['substitution_us_per_draw'], 5.0)
        # the embedded programs: ps_3_0, bound to their sources and headers (run_weapon_effects.py programs_current)
        for name in ('weapon_bullet_reshade_classic_ps', 'weapon_bullet_reshade_classic_flag_ps'):
            self.assertEqual(self.record['programs'][name]['target'], 'ps_3_0', name)

    def test_reshade_lone_classic_ap_pair(self):
        # ap-bullet-batch-site.md section 2: AP's standard_lighting / 2s DEFAULT PS (from the AP catalogues) behind the
        # shared VS: admitted under the AP row of the per-edition table and only there, the node body id +0x140, our
        # classic program on the model in their place
        self.assertTrue(dict(self.record['checks']).get('reshade_lone_classic_ap_pair'))
        programs = self.record['classic_programs_ap']['programs']
        self.assertEqual({k: v['hash'] for k, v in programs.items()},
                         {'ap_classic_vs.bin': '494fe349b8bc12ec', 'ap_classic_ps.bin': '7c83ed50c9894e44',
                          'ap_classic_ps2s.bin': 'e70adc744a38ca59'})
        rows = self.record['metrics']['reshade']
        case = next(x for x in rows if x.get('case') == 'lone_classic_ap')
        self.assertEqual((case['loaded'], case['game_hashes'], case['created'], case['node_body'], case['recognition'],
                          case['vanilla_finite']), (1, 1, 1, '0x140', 1, 1))
        self.assertLessEqual(case['worst_rel'], 1.0)
        recognition = [x for x in rows if x.get('case') == 'lone_classic_ap_recognition']
        self.assertEqual(len(recognition), 5)
        self.assertTrue(all(x['ok'] == 1 for x in recognition))

    def test_muzzle_reshade_gates(self):
        # docs/architecture/muzzle-flash-look.md (the re-shade): the ps_3_0 program created, the readback on the model,
        # the sequence bound and restored, energy <= mz_gain x vanilla's, the flash lit (peak >= mz_gain / 4), the extent
        # at 0.1 linear <= mz_size x vanilla's + 1 px (FL 65 A baked no white core), the cost measured
        checks = dict(self.record['checks'])
        for name in ('muzzle_reshade_programs_create', 'muzzle_reshade_program_matches_model',
                     'muzzle_reshade_standin_vanilla_is_emissive_law', 'muzzle_reshade_route_plain_binds_and_restores', 'muzzle_reshade_energy_within_gain',
                     'muzzle_reshade_extent_within_size', 'muzzle_reshade_cost_measured'):
            self.assertTrue(checks.get(name), name)
        rows = {x['case']: x for x in self.record['metrics']['muzzle_reshade'] if x['case'] != 'texel'}
        self.assertEqual((rows['programs']['created'], rows['programs']['ps_major']), (1, 3))
        self.assertLessEqual(rows['model']['worst_rel'], 1.0)
        # three poses (review F1): the flash axis 25 degrees out of the screen plane, along the view ray, in the plane;
        # the per-draw overlap grows from side-on to axis-on; body 445's stand-in (flash_*) and body 444's (flash444_*,
        # its own bodies[] row: the list of every classic flash body)
        for prefix, body in (('flash', 445), ('flash444', 444)):
            for pose in ('tilt', 'axis', 'side'):
                row = rows[f'{prefix}_{pose}']
                self.assertEqual(row['body'], body, pose)
                self.assertLessEqual(row['energy_ratio'], row['gain'], (prefix, pose))
                self.assertLessEqual(row['reshade_tenth_r'], row['size'] * row['vanilla_tenth_r'] + 1.0, (prefix, pose))
                self.assertGreaterEqual(row['reshade_peak'], 0.25 * row['gain'], (prefix, pose))
                self.assertLessEqual(row['reshade_peak_tenth_r'], row['vanilla_peak_tenth_r'], (prefix, pose))
            self.assertLess(rows[f'{prefix}_side']['overlap'], rows[f'{prefix}_tilt']['overlap'])
            self.assertLess(rows[f'{prefix}_tilt']['overlap'], rows[f'{prefix}_axis']['overlap'])
        self.assertNotEqual(rows['flash_tilt']['overlap'], rows['flash444_tilt']['overlap'])
        self.assertEqual(rows['cost']['fenced'], 1)
        self.assertEqual(rows['cost']['bodies'], 13)  # FL's seven and AP Mayhem's six (2026-11-13)
        self.assertLess(max(rows['cost']['match_last_us_per_draw'], rows['cost']['match_miss_us_per_draw']), 1.0)

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
                         ['one_capital_12hps', 'one_capital_12hps_lathe', 'one_capital_64_hits',
                          'one_capital_64_hits_lathe', 'sixteen_capitals_16_hits', 'sixteen_capitals_16_hits_lathe',
                          'station_flight_b_cluster_64_hits'])
        cpu = {x['case']: x for x in m['stage_cpu']}
        for x in m['stage_cpu']:  # the decal rows: stations' hits; the _lathe rows: ships with the Titan's profile
            self.assertEqual(x['lathes'], x['owners'] if x['case'].endswith('_lathe') else 0, x['case'])
            self.assertEqual(x['no_profile'], 0, x['case'])
            self.assertGreater(x['patches'], 0, x['case'])
        self.assertLessEqual(cpu['sixteen_capitals_16_hits_lathe']['mean_us'], cpu['sixteen_capitals_16_hits']['mean_us'])
        # The lathe bubble (2026-11-12): the Titan profile's units 1:1 with the FL 67 walk's measured box, its side hits
        # on the shell far nearer the contact than that box's ellipsoid's (the FL 67 witness), and the GPU draw where the
        # CPU maps it
        lathe = {x['case']: x for x in m['lathe']}
        self.assertLess(lathe['placement']['lathe_median_m'] * 4, lathe['placement']['walk_median_m'])
        bc = lathe['build_cost']                                                 # review F5: one Titan mesh, cold
        self.assertEqual((bc['built'], bc['builds_per_frame']), (1, 2))
        self.assertGreater(bc['best_us'], 0.0)
        self.assertLessEqual(lathe['gpu_top_lathe']['sky_gap_max_m'], lathe['gpu_summary']['hug_allowed_m'])
        self.assertGreater(lathe['gpu_top_ellipsoid']['sky_gap_max_m'], lathe['gpu_summary']['hug_allowed_m'])
        # Placement (user decision 2026-11-12 on FL 69 A: the walk removed): the station keeps the hull decal (nothing on
        # sky, nothing past the footprint, covered, the centroid within a cell); a capital ship without a profile draws
        # nothing (the hook forwards it native)
        place = {x['case']: x for x in m['placement']}
        st = place['station_flight_b_0p68R']
        self.assertEqual((st['surface'], st['lathes'], st['sky_lit'], st['outside_lit']), ('hull', 0, 0, 0))
        self.assertGreaterEqual(st['coverage'], 0.9)
        self.assertLessEqual(st['centroid_error_cells'], 1.0)
        cap = place['capital_titan_flank']
        self.assertEqual((cap['surface'], cap['patches'], cap['lit_pixels'], cap['lathes'], cap['no_profile']),
                         ('native', 0, 0, 0, 1))
        self.assertEqual(sorted(place), ['capital_titan_flank', 'station_flight_b_0p68R'])
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
        self.assertGreater(m['attach_slots'][0]['lathe_vs'], 0)                   # the lathe bubble (2026-11-12)
        self.assertLess(m['attach_slots'][0]['lathe_ps'], 2000)
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
