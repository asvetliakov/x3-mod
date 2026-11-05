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
                'tap_callee_esi_ebx_edi_return', 'weapon_pos_check_zero_error', 'no_projectile_class_forwards_unrecorded',
                'no_bullet_extension_read', 'nested_dying_projectile_skips_marked', 'nested_dying_victim_forwards_marked',
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
        age = m['age'][0]
        self.assertGreaterEqual(age['seta6_wall_ms'], 100.0)                       # measured, not computed
        self.assertLessEqual(age['seta6_wall_ms'], 100.2 + age['max_frame_ms'] + 0.5)
        self.assertEqual((m['retired'][0]['retired'], m['retired'][0]['ring_live'], m['retired'][0]['drew']), (1, 0, 0))
        self.assertEqual(m['refused'][0]['reason'], 'program_create')
        cases = {(x['width'], x['case']) for x in m['timing']}
        for width in (1920, 5120):
            for case in ('fence', 'two', 'two_cull_none', 'fullscreen', 'fullscreen_cull_none'):
                self.assertIn((width, case), cases)

    def test_bound_to_its_production_sources(self):
        import run_weapon_effects as runner
        source = self.record['source']
        self.assertEqual(self.record['production_sources'], list(runner.PRODUCTION_SOURCES))
        for path, recorded in source['sha256'].items():
            self.assertEqual(sha(path), recorded, path + ': changed since the record: rerun run_weapon_effects.py --host native')


if __name__ == '__main__':
    unittest.main()
