"""Field-of-view patch Wine fixture record (run_fov_patch.py): strict JSON, bottle, pass counts, the immediate the
executed constructor stored per step, install/restore/confirm rows, the build audit and the source binding by content hash."""
import hashlib
import json
import unittest
from collections import Counter
from pathlib import Path

import run_fov_patch as runner

ROOT = Path(__file__).resolve().parents[2]
RECORD = ROOT / 'verification/results/bottle-X3/fov-patch.json'
CHECKS = 134


def strict(name):
    raise ValueError(f'non-standard JSON constant {name}')


def record_problems(r):
    """Why a record is not a clean X3 pass (empty when it is): verdict, counts, bottle and emulation lines."""
    problems = []
    if r.get('passed') is not True:
        problems.append('passed is not true')
    if r.get('exit_status') != 0 or 'crash' in r:
        problems.append('fixture did not exit cleanly')
    checks = r.get('checks') or []
    passed = sum(1 for c in checks if c.get('pass') is True)
    if not (r.get('check_count') == r.get('pass_count') == len(checks) == passed == CHECKS):
        problems.append(f"counts check_count={r.get('check_count')} pass_count={r.get('pass_count')} rows={len(checks)} passing={passed} expected={CHECKS}")
    if r.get('result') != {'checks': CHECKS, 'failures': 0}:
        problems.append(f"fixture RESULT {r.get('result')}")
    b = r.get('bottle') or {}
    if (b.get('name'), b.get('wine_arch')) != ('X3', 'arm64'):
        problems.append('bottle is not X3 arm64')
    if b.get('environment') != {'FEX_X87REDUCEDPRECISION': '1', 'WINEMSYNC': '1'}:
        problems.append(f"emulation environment {b.get('environment')}")
    return problems


class FovPatchRecordTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.record = json.loads(RECORD.read_text(), parse_constant=strict)

    def construction(self, memory, step):
        rows = [c for c in self.record['constructions'] if (c['memory'], c['step']) == (memory, step)]
        self.assertEqual(len(rows), 1, (memory, step))
        self.assertTrue(rows[0]['fields'], (memory, step))
        return rows[0]['focus']

    def test_run_and_bottle(self):
        r = self.record
        self.assertEqual(record_problems(r), [])
        self.assertFalse(r['game_launched'])
        self.assertRegex(r['executable_sha256'], r'^[0-9a-f]{64}$')
        audit = r['build_audit']
        self.assertEqual((audit['overlapping_sections'], audit['module_imports_own_protect_or_flush'], len(audit['module_calls_seam'])), ([], [], 4))
        self.assertTrue(all(s['ok'] for s in audit['sections'].values()))

    def test_validator_rejects_failed_or_inconsistent_records(self):
        def mutated(change):
            copy = json.loads(json.dumps(self.record))
            change(copy)
            return record_problems(copy)
        self.assertTrue(mutated(lambda r: r.update(passed=False)))
        self.assertTrue(mutated(lambda r: r.update(pass_count=r['pass_count'] - 1)))
        self.assertTrue(mutated(lambda r: r['checks'][0].update({'pass': False})))
        self.assertTrue(mutated(lambda r: r['checks'].pop()))
        self.assertTrue(mutated(lambda r: r.update(result={'checks': CHECKS, 'failures': 1})))
        self.assertTrue(mutated(lambda r: r['bottle']['environment'].update(WINEMSYNC='0')))
        self.assertTrue(mutated(lambda r: r['bottle'].update(name='Steam')))

    def test_bound_to_its_production_sources(self):
        source = self.record['source']
        self.assertRegex(source['commit'], r'^[0-9a-f]{40}$')
        self.assertEqual(self.record['production_sources'], list(runner.PRODUCTION_SOURCES))
        now = {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in runner.PRODUCTION_SOURCES}
        self.assertEqual(now, source['sha256'], 'production sources changed since the record: rerun run_fov_patch.py')

    def test_pass_counts(self):
        r = self.record
        self.assertTrue(all(c['pass'] for c in r['checks']))
        names = [c['name'] for c in r['checks']]
        self.assertEqual(len(names), len(set(names)))
        for name in ('pages_at_engine_vas', 'engine_imm32_offset_4_of_its_qword', 'engine_page_is_mem_image_execute_read', 'default_unset_off_row_and_untouched',
                     'engine_value_off_row_and_untouched', 'refuse_below_36_row_and_untouched', 'refuse_above_120_row_and_untouched',
                     'refuse_reader_mismatch_untouched', 'install_ok_atomic_registry_absent_row', 'install_sequence_counts',
                     'install_readback_70340000_rest_of_page_unchanged', 'engine_after_patch_ctor_stores_3470', 'confirm_absent_row',
                     'confirm_registry_row_match', 'setfocus_overrides_base_patch_stays', 'restore_row', 'registry_written_row_and_field',
                     'registry_readonly_skipped_row_and_field', 'rollback_readback_rolled_back', 'rollback_failed_registered',
                     'restore_other_value_not_owned_untouched', 'private_install_ok_atomic_ctor_5eb4', 'late_initialize_refused',
                     'install_lasterror_preserved', 'restore_lasterror_preserved'):
            self.assertIn(name, names)

    def test_recorded_sequence_and_override(self):
        self.assertEqual(self.record['install_sequence'], {'step': 'install', 'protects': 2, 'reads': 4, 'writes': 1, 'atomic': 1, 'flushes': 1})
        self.assertEqual(self.record['setfocus_override'], {'setfocus': '0x471c', 'base': '0x471c', 'current': '0x471c', 'imm32': '70340000'})
        self.assertEqual(sorted(r['found'] or '--' for r in self.record['restore_rows'] if r['status'] == 'restore_not_owned'),
                         ['--', '71340000', '71340000'])   # the failed rollback's bytes, a foreign 0x3471, the unreadable span

    def test_executed_constructor(self):
        for memory, step, focus in (('engine', 'before', '0x4000'), ('engine', 'after_patch', '0x3470'), ('engine', 'after_restore', '0x4000'),
                                    ('engine', 'after_protect_failed', '0x4000'), ('engine', 'after_rollback', '0x4000'),
                                    ('engine', 'after_rollback_unprotected', '0x4000'), ('engine', 'after_rollback_failed', '0x3471'),
                                    ('engine', 'after_late', '0x4000'), ('private', 'before', '0x4000'), ('private', 'after_patch', '0x5eb4'),
                                    ('private', 'after_restore', '0x4000')):
            self.assertEqual(self.construction(memory, step), focus, (memory, step))

    def test_log_rows(self):
        installs = self.record['install_rows']
        self.assertTrue(all(row['site'] == '0041c9dc' for row in installs))
        counts = Counter((row['status'], row['reason'], row['registry']) for row in installs)
        self.assertEqual(counts[('patched', 'ok', 'absent')], 3)
        self.assertEqual(counts[('patched', 'ok', 'written')], 2)
        self.assertEqual(counts[('patched', 'ok', 'skipped')], 4)
        self.assertEqual(counts[('off', 'game', 'skipped')], 2)
        self.assertEqual(counts[('off', 'engine_value', 'skipped')], 1)
        self.assertEqual(counts[('refused', 'out_of_range', 'skipped')], 2)
        self.assertEqual(counts[('patched_unverified', 'rollback_failed', 'skipped')], 1)
        self.assertEqual(counts[('refused', 'late_claim', 'skipped')], 1)
        self.assertTrue(all(row['value'] == {'patched': '0x3470', 'patched_unverified': '0x3471'}.get(row['status'], '0x4000') for row in installs))
        self.assertEqual([(c['registry'], c['focus'], c['match']) for c in self.record['confirm_rows']], [('absent', None, False), ('present', '0x3470', True)])
        restores = self.record['restore_rows']
        self.assertTrue(restores and all(row['site'] in ('0041c9dc',) or row['site'].endswith('9dc') for row in restores))
        self.assertIn(('restore_not_owned', '71340000', True), [(r['status'], r['found'], r['registered']) for r in restores])


if __name__ == '__main__':
    unittest.main()
