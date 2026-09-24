"""Field-of-view patch Wine fixture record (run_fov_patch.py): strict JSON, bottle, pass counts, the immediate the
executed constructor stored per step, the base each executed INS_SetFocus stored (remap, pass-through, registers kept),
install/restore/confirm rows, the build audit and the source binding by content hash."""
import hashlib
import json
import unittest
from collections import Counter
from pathlib import Path

import run_fov_patch as runner

ROOT = Path(__file__).resolve().parents[2]
RECORD = ROOT / 'verification/results/bottle-X3/fov-patch.json'
CHECKS = 194


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
        self.assertEqual(sorted(audit['sections']), ['.x3mfvc', '.x3mfvd', '.x3mfvr', '.x3mfvs', '.x3mfvx'])

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
                     'setfocus_case_and_callee_bytes_at_engine_vas_site_qword_aligned', 'setfocus_before_install_stores_471c_registers_kept',
                     'refuse_below_70_row_and_untouched', 'refuse_above_100_row_and_untouched', 'refuse_old_vertical_value_row_and_untouched',
                     'refuse_reader_mismatch_untouched', 'refuse_setfocus_case_mismatch_untouched', 'refuse_setfocus_site_mismatch_untouched',
                     'refuse_setfocus_callee_mismatch_untouched', 'install_ok_atomic_registry_absent_row', 'install_sequence_counts',
                     'install_setfocus_jmp_to_arena_rest_of_page_unchanged', 'install_readback_70340000_rest_of_page_unchanged',
                     'engine_after_patch_ctor_stores_3470', 'confirm_absent_row', 'confirm_registry_row_match',
                     'setfocus_n70_0x31c7_to_0x2768', 'setfocus_n90_0x4000_to_0x3470', 'setfocus_n100_0x471c_to_0x3b6f',
                     'setfocus_n45_passthrough_0x2000_to_0x2000', 'setfocus_garbage_passthrough_0xfffffff0_to_0xfffffff0',
                     'setfocus_every_n_50_130_matches_the_formula', 'setfocus_n50_table_floor_0x238e_to_0x1b6a',
                     'setfocus_n130_table_ceiling_0x5c71_to_0x52ab', 'setfocus_n49_passthrough_0x22d8_to_0x22d8',
                     'setfocus_n131_passthrough_0x5d27_to_0x5d27', 'setfocus_menu_100_base_3b6f_patch_stays', 'restore_row',
                     'restore_setfocus_bytes_back_vanilla_store_471c', 'registry_written_row_and_field', 'registry_readonly_skipped_row_and_field',
                     'rollback_readback_rolled_back', 'rollback_failed_registered', 'restore_other_value_not_owned_untouched',
                     'setfocus_readback_both_sites_rolled_back', 'setfocus_failed_constructor_rollback_failed_registered',
                     'setfocus_restore_not_owned_foreign_jmp_untouched_registered', 'setfocus_restore_after_not_owned_bytes_back',
                     'install_setting_70_ctor_0x2768', 'install_setting_100_ctor_0x3b6f', 'private_install_ok_atomic_ctor_5eb4',
                     'late_initialize_refused', 'setfocus_after_late_vanilla_store_471c', 'install_lasterror_preserved', 'restore_lasterror_preserved'):
            self.assertIn(name, names)

    def test_recorded_sequence_and_override(self):
        self.assertEqual(self.record['install_sequence'], {'step': 'install', 'protects': 2, 'reads': 7, 'writes': 1, 'atomic': 1, 'flushes': 1})
        self.assertEqual(self.record['setfocus_override'], {'setfocus': '0x471c', 'base': '0x3b6f', 'current': '0x3b6f', 'imm32': '70340000'})
        self.assertEqual(sorted(r['found'] or '--' for r in self.record['restore_rows'] if r['status'] == 'restore_not_owned'),
                         ['--', '71340000', '71340000'])   # the failed rollback's bytes, a foreign 0x3471, the unreadable span

    def test_executed_setfocus(self):
        runs = {r['step']: (r['in'], r['out']) for r in self.record['setfocus_runs']}
        self.assertTrue(self.record['setfocus_runs'] and all(r['preserved'] for r in self.record['setfocus_runs']))
        for step, pair in (('before', ('0x471c', '0x471c')), ('patched_n70', ('0x31c7', '0x2768')), ('patched_n90', ('0x4000', '0x3470')),
                           ('patched_n100', ('0x471c', '0x3b6f')), ('patched_below_table_passthrough', ('0x2333', '0x2333')),
                           ('patched_above_table_passthrough', ('0x5ccd', '0x5ccd')), ('patched_n49_passthrough', ('0x22d8', '0x22d8')),
                           ('patched_n131_passthrough', ('0x5d27', '0x5d27')), ('patched_n50_table_floor', ('0x238e', '0x1b6a')), ('patched_n130_table_ceiling', ('0x5c71', '0x52ab')), ('patched_n45_passthrough', ('0x2000', '0x2000')),
                           ('patched_garbage_passthrough', ('0xfffffff0', '0xfffffff0')), ('after_restore', ('0x471c', '0x471c')),
                           ('after_setfocus_rollback', ('0x471c', '0x471c')), ('after_late', ('0x471c', '0x471c'))):
            self.assertEqual(runs[step], pair, step)

    def test_executed_constructor(self):
        for memory, step, focus in (('engine', 'before', '0x4000'), ('engine', 'after_patch', '0x3470'), ('engine', 'after_restore', '0x4000'),
                                    ('engine', 'after_protect_failed', '0x4000'), ('engine', 'after_rollback', '0x4000'),
                                    ('engine', 'after_rollback_unprotected', '0x4000'), ('engine', 'after_rollback_failed', '0x3471'),
                                    ('engine', 'after_late', '0x4000'), ('engine', 'after_setfocus_rollback', '0x4000'),
                                    ('engine', 'install_setting_70_ctor_0x2768', '0x2768'), ('engine', 'install_setting_100_ctor_0x3b6f', '0x3b6f'),
                                    ('private', 'before', '0x4000'), ('private', 'after_patch', '0x5eb4'),
                                    ('private', 'after_restore', '0x4000')):
            self.assertEqual(self.construction(memory, step), focus, (memory, step))

    def test_log_rows(self):
        installs = self.record['install_rows']
        self.assertTrue(all(row['site'] == '0041c9dc' for row in installs))
        counts = Counter((row['status'], row['reason'], row['registry']) for row in installs)
        self.assertEqual(counts[('patched', 'ok', 'absent')], 7)
        self.assertEqual(counts[('patched', 'ok', 'written')], 2)
        self.assertEqual(counts[('patched', 'ok', 'skipped')], 4)
        self.assertEqual(counts[('off', 'game', 'skipped')], 2)
        self.assertEqual(counts[('refused', 'out_of_range', 'skipped')], 3)
        self.assertEqual(counts[('refused', 'setfocus_mismatch', 'skipped')], 3)
        self.assertEqual(counts[('patched_unverified', 'rollback_failed', 'skipped')], 1)
        self.assertEqual(counts[('refused', 'setfocus_failed', 'skipped')], 1)
        self.assertEqual(counts[('patched_unverified', 'setfocus_failed', 'skipped')], 1)
        self.assertEqual(counts[('refused', 'late_claim', 'skipped')], 1)
        # Every live install carries both sites; a refusal before the constructor write never touches the second.
        self.assertTrue(all((row['setfocus'], row['setfocus_write']) == ('active', 'atomic') for row in installs if row['status'] == 'patched'))
        self.assertEqual(sorted(row['setfocus'] for row in installs if row['reason'] == 'setfocus_failed'), ['readback_failed', 'readback_failed'])
        self.assertEqual({row['value'] for row in installs if row['status'] == 'patched'}, {'0x3470', '0x2768', '0x3b6f', '0x28f8'})
        self.assertTrue(all(row['value'] == '0x4000' for row in installs if row['status'] in ('off', 'refused')))
        self.assertEqual([(c['registry'], c['focus'], c['match']) for c in self.record['confirm_rows']], [('absent', None, False), ('present', '0x3470', True)])
        restores = self.record['restore_rows']
        self.assertTrue(restores and all(row['site'] in ('0041c9dc',) or row['site'].endswith('9dc') for row in restores))
        self.assertIn(('restore_not_owned', '71340000', True), [(r['status'], r['found'], r['registered']) for r in restores])
        self.assertIn(('restored', True, 'restore_not_owned'), [(r['status'], r['registered'], r['setfocus']) for r in restores])
        self.assertIn(('none', False, 'restored'), [(r['status'], r['registered'], r['setfocus']) for r in restores])


if __name__ == '__main__':
    unittest.main()
