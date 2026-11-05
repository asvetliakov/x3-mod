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
                'shields_default_forwards_native', 'shield_hit_stage_and_forward', 'shield_hit_forward_callee_state',
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
                'fixture_stack_margin'}

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
        self.assertEqual(a['handler_calls'], {'_x3m_weapon_event': [], '_x3m_weapon_tap': []})
        self.assertTrue(a['stub_ok'])
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
        seam = m['seam'][0]
        self.assertEqual(seam['pair_draws'], 2)
        self.assertLessEqual(seam['peak_pair'], max(seam['peak_a'], seam['peak_b']) * 1.002 + 1e-3)
        self.assertEqual(sorted(x['case'] for x in m['stage_cpu']), ['one_capital_64_hits', 'sixteen_capitals_16_hits'])
        fade = m['fade']
        self.assertEqual(fade['t%d' % fade['lifetime_ms']], 0.0)                 # nothing left at the record's lifetime
        age = m['age'][0]
        self.assertGreaterEqual(age['seta6_wall_ms'], age['seta6_expected_wall_ms'])  # measured, not computed
        self.assertLessEqual(age['seta6_wall_ms'], age['seta6_expected_wall_ms'] + 0.2 + age['max_frame_ms'] + 0.5)
        self.assertEqual((m['retired'][0]['retired'], m['retired'][0]['ring_live'], m['retired'][0]['drew']), (1, 0, 0))
        self.assertEqual(m['refused'][0]['reason'], 'program_create')
        self.assertLess(m['attach'][0]['shell_ps'], 400)                          # low hundreds of slots
        cases = {(x['width'], x['case']) for x in m['timing']}
        for width in (1920, 5120):
            for case in ('fence', 'fighter_two_hits_one_region', 'fighter_two_regions',
                         'capital_close_two_hits_one_region', 'capital_close_two_regions', 'worst_four_regions',
                         'fighter_12hps_map', 'capital_close_full_map', 'capital_fill_full_map', 'capital_spread_spill'):
                self.assertIn((width, case), cases)
        fill = [x for x in m['timing'] if x['case'] == 'capital_fill_full_map']
        self.assertTrue(all(x['raster_share'] >= 0.95 for x in fill))             # its cap really fills the frame

    def test_bound_to_its_production_sources(self):
        import run_weapon_effects as runner
        source = self.record['source']
        self.assertEqual(self.record['production_sources'], list(runner.PRODUCTION_SOURCES))
        for path, recorded in source['sha256'].items():
            self.assertEqual(sha(path), recorded, path + ': changed since the record: rerun run_weapon_effects.py --host native')


if __name__ == '__main__':
    unittest.main()
