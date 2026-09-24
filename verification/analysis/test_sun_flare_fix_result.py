"""Lens-flare collector fix Wine fixture record (run_sun_flare_fix.py): strict JSON, bottle, pass counts,
the section 9.1 vectors per step, the sweep comparison, install/restore rows, the build audit and the
source binding by content hash."""
import hashlib
import json
import unittest
from pathlib import Path

import run_sun_flare_fix as runner

ROOT = Path(__file__).resolve().parents[2]
RECORD = ROOT / 'verification/results/bottle-X3/sun-flare-fix.json'
CHECKS = 79
CASES = ['A_run309', 'B_below_zcrit', 'C_vanilla_bug', 'D_off_left', 'E_inside_edge', 'E2_largest_x', 'F_off_top', 'G_no_overflow', 'H_at_bound']
VANILLA = {'A_run309': 2, 'B_below_zcrit': 1, 'C_vanilla_bug': 2, 'D_off_left': 2, 'E_inside_edge': 1, 'E2_largest_x': 2, 'F_off_top': 2,
           'G_no_overflow': 1, 'H_at_bound': 1}
FIXED = dict(VANILLA, A_run309=1, C_vanilla_bug=1, E2_largest_x=1)   # F stays off (by y), D stays off (by x)


def strict(name):
    raise ValueError(f'non-standard JSON constant {name}')


class SunFlareFixRecordTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.record = json.loads(RECORD.read_text(), parse_constant=strict)

    def test_run_and_bottle(self):
        r = self.record
        self.assertIs(r['passed'], True)
        self.assertEqual((r['exit_status'], 'crash' in r, r['game_launched']), (0, False, False))
        self.assertEqual(r['check_count'], CHECKS)
        self.assertEqual(r['pass_count'], CHECKS)
        self.assertEqual(sum(c['pass'] for c in r['checks']), CHECKS)
        self.assertEqual(r['result'], {'checks': CHECKS, 'failures': 0})
        self.assertEqual((r['bottle']['name'], r['bottle']['wine_arch']), ('X3', 'arm64'))
        self.assertEqual(r['bottle']['environment'], {'FEX_X87REDUCEDPRECISION': '1', 'WINEMSYNC': '1'})

    def test_bound_to_its_production_sources(self):
        source = self.record['source']
        self.assertEqual(tuple(source['sha256']), runner.PRODUCTION_SOURCES)
        now = {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in runner.PRODUCTION_SOURCES}
        self.assertEqual(now, source['sha256'], 'production sources changed since the record: rerun run_sun_flare_fix.py')

    def test_vectors_per_step(self):
        steps = {}
        for v in self.record['vectors']:
            steps.setdefault(v['step'], {})[v['case']] = v
        self.assertEqual(set(steps), {'vanilla', 'patched', 'restored', 'after_rollbacks', 'rollback_failed_tail_only', 'after_late'})
        for step, rows in steps.items():
            self.assertEqual(list(rows), CASES, step)
            want = FIXED if step == 'patched' else VANILLA
            self.assertEqual({c: rows[c]['pad'] for c in CASES}, want, step)
            self.assertTrue(all(rows[c]['ok'] and rows[c]['regs'] for c in CASES), step)
        # E2: vanilla leaves at the x test with ECX = |x|/2 = 2^30 - 1; patched passes it and leaves at the y test (ECX = |y|/2 = 0).
        self.assertEqual((steps['vanilla']['E2_largest_x']['ecx'], steps['patched']['E2_largest_x']['ecx']), ('3fffffff', '00000000'))

    def test_sweep_and_comparison(self):
        sweeps = self.record['sweeps']
        self.assertEqual(len(sweeps), 6)
        self.assertTrue(all(s['vectors'] == s['model_matches'] == 4096 for s in sweeps))
        c = self.record['compare']
        self.assertEqual(c['non_overflowing'] + c['overflowing'], 4096)
        self.assertEqual(c['identical'], c['non_overflowing'])
        self.assertEqual(c['past_x_patched'], c['overflowing'])
        self.assertGreater(c['non_overflowing'], 100)
        self.assertGreater(c['changed'], 100)

    def test_log_rows(self):
        installs = self.record['install_rows']
        patched = [r for r in installs if r['status'] == 'patched']
        self.assertTrue(patched and all(r['reason'] == 'ok' and r['write'] == 'atomic' and r['stub'] != '00000000' for r in patched))
        reasons = {r['reason'] for r in installs if r['status'] != 'patched'}
        self.assertLessEqual({'off', 'invalid_setting', 'too_long', 'executable_mismatch', 'bytes_mismatch', 'chain_failed', 'readback_mismatch',
                              'rollback_failed', 'late_claim'}, reasons)
        statuses = {r['status'] for r in self.record['restore_rows']}
        self.assertEqual(statuses, {'restored', 'restore_failed', 'restore_not_owned'})
        self.assertTrue(all(r['site'] == '0047e391' for r in installs + self.record['restore_rows']))

    def test_build_audit(self):
        a = self.record['build_audit']
        self.assertEqual(a['module_calls_seam'], ['fixture_read_code', 'fixture_store_pointer', 'fixture_restore'])
        self.assertEqual((a['module_calls_engine_patch_directly'], a['overlapping_sections']), ([], []))
        self.assertGreaterEqual(a['engine_patch_lock_cmpxchg8b'], 1)
        self.assertEqual((a['engine_page_va'], a['x3msfc']['va'], a['x3msfc']['size'], a['x3msfc']['code_readonly'], a['gate_at_engine_offset']),
                         ('0047e000', '0047e000', 4096, True, True))
        self.assertEqual(self.record['memory'], [{'memory': 'engine', 'type': '0x1000000', 'protect': '0x20', 'allocation_protect': '0x80'}])

    def test_parse(self):
        report = runner.parse('\n'.join((
            'CHECK pass a', 'CHECK FAIL b detail here',
            'VECTOR step=patched case=A_run309 pad=1 want=1 eax=233438e8 ecx=00000000 regs=1 ok=1',
            'SWEEP step=patched vectors=4 model_matches=4 overflowing=2',
            'LOG sun_flare_fix site=0047e391 status=patched reason=ok mode=on setting=on write=atomic stub=00a300d0',
            'LOG sun_flare_fix_restore site=0047e391 status=restore_failed found=e9321d5b00c8 registered=1', 'RESULT checks=2 failures=1')))
        self.assertEqual([c['pass'] for c in report['checks']], [True, False])
        self.assertEqual(report['vectors'][0]['pad'], 1)
        self.assertEqual(report['sweeps'], [{'step': 'patched', 'vectors': 4, 'model_matches': 4, 'overflowing': 2}])
        self.assertEqual(report['install_rows'][0]['stub'], '00a300d0')
        self.assertEqual(report['restore_rows'][0], {'site': '0047e391', 'status': 'restore_failed', 'found': 'e9321d5b00c8', 'registered': True})
        self.assertEqual(report['result'], {'checks': 2, 'failures': 1})


if __name__ == '__main__':
    unittest.main()
