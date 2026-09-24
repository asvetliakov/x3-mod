"""LOD occlusion patch Wine fixture record (run_lod_occlusion_patch.py): strict JSON, bottle, pass counts,
executed paths, install/restore rows, protections, the build audit and the source binding by content hash."""
import hashlib
import json
import unittest
from collections import Counter
from pathlib import Path

import run_lod_occlusion_patch as runner

ROOT = Path(__file__).resolve().parents[2]
RECORD = ROOT / 'verification/results/bottle-X3/lod-occlusion-patch.json'
CHECKS = 108  # 100 + the four default_marker_* cases (row and LastError each), Run 81
PAGE_EXECUTE_READ, PAGE_EXECUTE_READWRITE, PAGE_EXECUTE_WRITECOPY = 0x20, 0x40, 0x80


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


class LodOcclusionPatchRecordTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.record = json.loads(RECORD.read_text(), parse_constant=strict)

    def branch(self, memory, step):
        rows = [b for b in self.record['branches'] if (b['memory'], b['step']) == (memory, step)]
        self.assertEqual(len(rows), 1, (memory, step))
        return rows[0]['lod0'], rows[0]['lod1']

    def test_run_and_bottle(self):
        r = self.record
        self.assertEqual(record_problems(r), [])
        self.assertFalse(r['game_launched'])
        self.assertRegex(r['executable_sha256'], r'^[0-9a-f]{64}$')

    def test_validator_rejects_failed_or_inconsistent_records(self):
        def mutated(change):
            copy = json.loads(json.dumps(self.record))
            change(copy)
            return record_problems(copy)
        self.assertTrue(mutated(lambda r: r.update(passed=False)))
        self.assertTrue(mutated(lambda r: r.update(pass_count=r['pass_count'] - 1)))
        self.assertTrue(mutated(lambda r: r.update(check_count=r['check_count'] + 1)))
        self.assertTrue(mutated(lambda r: r['checks'][0].update({'pass': False})))
        self.assertTrue(mutated(lambda r: r['checks'].pop()))
        self.assertTrue(mutated(lambda r: r.update(result={'checks': CHECKS, 'failures': 1})))
        self.assertTrue(mutated(lambda r: r['bottle']['environment'].update(WINEMSYNC='0')))
        self.assertTrue(mutated(lambda r: r['bottle'].update(name='Steam')))

    def test_bound_to_its_production_sources(self):
        # The record stays valid only while the linked production sources hash as they did when it was made.
        source = self.record['source']
        self.assertRegex(source['commit'], r'^[0-9a-f]{40}$')
        self.assertEqual(self.record['production_sources'], list(runner.PRODUCTION_SOURCES))
        self.assertEqual(set(source['sha256']), set(runner.PRODUCTION_SOURCES))
        now = {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in runner.PRODUCTION_SOURCES}
        self.assertEqual(now, source['sha256'], 'production sources changed since the record: rerun run_lod_occlusion_patch.py')

    def test_pass_counts(self):
        r = self.record
        self.assertEqual((r['check_count'], r['pass_count'], len(r['checks'])), (CHECKS, CHECKS, CHECKS))
        self.assertTrue(all(c['pass'] for c in r['checks']))
        names = [c['name'] for c in r['checks']]
        self.assertEqual(len(names), len(set(names)))
        for name in ('engine_page_at_engine_va', 'placeholder_global_at_engine_va', 'engine_window_at_window_va_is_expected_window',
                     'engine_rel32_offset_1_of_its_qword', 'default_unset_off_row_and_untouched', 'install_ok_atomic_row', 'install_sequence_counts',
                     'install_readback_00000000_rest_of_page_unchanged', 'engine_after_patch_lod0_path_for_lod1', 'restore_row',
                     'rollback_readback_rolled_back', 'rollback_dropped_write_rolled_back', 'rollback_unprotected_original_bytes_page_writable',
                     'rollback_failed_registered', 'restore_dropped_write_failed_registered', 'restore_not_owned_foreign_bytes_untouched_registered', 'restore_after_failures_c9000000', 'restore_unreadable_found_unread_no_write', 'refuse_protect_failed_untouched',
                     'refuse_changed_window_untouched', 'refuse_already_patched_window_untouched', 'roview_raise_refused_by_os',
                     'roview_install_protect_failed', 'private_install_ok_atomic_readback', 'late_initialize_refused', 'late_install_at_refused',
                     'install_lasterror_preserved', 'restore_lasterror_preserved'):
            self.assertIn(name, names)

    def test_executed_paths(self):
        # lod0/lod1: the path a node with LOD index 0 / 1 took (1 = LOD-0 bind path, 2 = placeholder bind).
        vanilla, patched = (1, 2), (1, 1)
        for memory, step, expected in (('engine', 'before', vanilla), ('engine', 'after_patch', patched), ('engine', 'after_restore', vanilla),
                                       ('engine', 'after_rollback', vanilla), ('engine', 'after_rollback_unprotected', vanilla),
                                       ('engine', 'after_restore_after_failures', vanilla), ('engine', 'after_protect_failed', vanilla),
                                       ('engine', 'after_patch_flushed', patched), ('engine', 'after_restore_flushed', vanilla),
                                       ('engine', 'after_late', vanilla), ('private', 'before', vanilla), ('private', 'after_patch', patched),
                                       ('private', 'after_restore', vanilla), ('hot', 'after_raw_patch_flushed', patched),
                                       ('hot', 'after_raw_restore_flushed', vanilla)):
            self.assertEqual(self.branch(memory, step), expected, (memory, step))
        self.assertEqual(self.branch('engine', 'after_rollback_failed'), (None, None))  # jne +0xc8 is not executed
        self.assertEqual(set(self.record['flush']), {'patch_without_flush_lod1', 'restore_without_flush_lod1', 'raw_patch_lod1', 'raw_restore_lod1'})

    def test_log_rows(self):
        installs = self.record['install_rows']
        self.assertTrue(all(row['site'] == '004c34f7' for row in installs))
        self.assertEqual(Counter((row['status'], row['reason'], row['write']) for row in installs), Counter({
            ('patched', 'ok', 'atomic'): 4, ('off', 'record0', 'none'): 4, ('refused', 'bytes_mismatch', 'none'): 2,
            ('refused', 'patch_rolled_back', 'atomic'): 2, ('refused', 'invalid_setting', 'none'): 1, ('refused', 'too_long', 'none'): 1,
            ('refused', 'executable_mismatch', 'none'): 3, ('refused', 'protect_failed', 'none'): 1, ('refused', 'rollback_unprotected', 'atomic'): 1,
            ('patched_unverified', 'rollback_failed', 'atomic'): 1, ('refused', 'late_claim', 'none'): 1}))
        # The launcher's default marker reaches the row only with a value and marker 1 (default_marker_executable_mismatch, _record0).
        self.assertEqual(Counter(row['default'] for row in installs), Counter({False: len(installs) - 2, True: 2}))
        restores = self.record['restore_rows']
        # Every restore names the engine's jne except the private page's, which names its own copy at the same page offset.
        self.assertEqual(Counter(row['site'] == '004c34f7' for row in restores), Counter({True: 9, False: 1}))
        self.assertTrue(all(int(row['site'], 16) & 0xfff == 0x4f7 for row in restores))
        self.assertEqual(Counter((row['status'], row['found'], row['registered']) for row in restores), Counter({
            ('restored', '00000000', False): 5, ('restore_failed', '00000000', True): 2, ('restore_not_owned', 'c8000000', True): 1,
            ('restored', 'c9000000', False): 1, ('restore_not_owned', None, True): 1}))
        # restore_not_owned never writes: the foreign bytes stay and the site stays registered (engine_patch::restore rule).
        self.assertTrue(all(row['registered'] for row in restores if row['status'] != 'restored'))

    def test_protections(self):
        rows = {(p['memory'], p['step']): {k: v for k, v in p.items() if k not in ('memory', 'step')} for p in self.record['protect']}
        engine, private = rows[('engine', 'install')], rows[('private', 'install')]
        self.assertEqual((int(engine['previous'], 16), int(engine['after'], 16)), (PAGE_EXECUTE_READ, PAGE_EXECUTE_READ))
        self.assertIn(int(engine['during'], 16), (PAGE_EXECUTE_READWRITE, PAGE_EXECUTE_WRITECOPY))
        self.assertEqual((int(private['previous'], 16), int(private['during'], 16), int(private['after'], 16)),
                         (PAGE_EXECUTE_READ, PAGE_EXECUTE_READWRITE, PAGE_EXECUTE_READ))
        memory = {m['memory']: m for m in self.record['memory']}
        self.assertEqual(int(memory['engine']['type'], 16), 0x1000000)   # MEM_IMAGE
        self.assertEqual(int(memory['private']['type'], 16), 0x20000)    # MEM_PRIVATE
        self.assertEqual((self.record['roview']['raise'] == 0, self.record['roview']['error'] != 0), (True, True))

    def test_build_audit(self):
        audit = self.record['build_audit']
        self.assertEqual(audit['module_imports_own_protect_or_flush'], [])
        self.assertEqual(len(audit['module_calls_seam']), 4)
        self.assertGreaterEqual(audit['engine_patch_lock_cmpxchg8b'], 1)
        self.assertEqual((audit['engine_page_va'], audit['x3mocc']['va'], audit['x3mocc']['size']), ('004c3000', '004c3000', 4096))
        self.assertEqual((audit['placeholder_global_va'], audit['x3mocd']['va'], audit['x3mocd']['size']), ('00606f74', '00606000', 4096))
        self.assertEqual(audit['overlapping_sections'], [])


class LodOcclusionPatchParserTests(unittest.TestCase):
    def test_parse(self):
        stdout = '\n'.join((
            'CHECK pass install_ok_atomic_row', 'CHECK FAIL restore_row detail text',
            'BRANCH memory=engine step=after_patch lod0=1 lod1=1',
            'BRANCH memory=engine step=after_rollback_failed lod0=- lod1=- (jne +0xc8 not executed: mid-instruction target)',
            'PROTECT memory=engine step=install previous=0x20 during=0x80 after=0x20', 'ROVIEW protect=0x20 raise=0 error=87',
            'FLUSH patch_without_flush_lod1=1 (1 = new rel32 executed) restore_without_flush_lod1=2 (2 = old rel32 executed)',
            'FLUSH_RAW patch_lod1=1 restore_lod1=2', 'TIMING install_us=226.4 restore_us=184.3',
            'LOG lod_occlusion site=004c34f7 status=patched reason=ok mode=all setting=all write=atomic',
            'LOG lod_occlusion_restore site=004c34f7 status=restore_failed found=c8000000 registered=1', 'RESULT checks=2 failures=1'))
        report = runner.parse(stdout)
        self.assertEqual(report['checks'], [{'name': 'install_ok_atomic_row', 'pass': True}, {'name': 'restore_row', 'pass': False, 'detail': 'detail text'}])
        self.assertEqual([(b['lod0'], b['lod1']) for b in report['branches']], [(1, 1), (None, None)])
        self.assertEqual(report['flush'], {'patch_without_flush_lod1': 1, 'restore_without_flush_lod1': 2, 'raw_patch_lod1': 1, 'raw_restore_lod1': 2})
        self.assertEqual(report['roview'], {'protect': '0x20', 'raise': 0, 'error': 87})
        self.assertEqual(report['install_rows'][0]['site'], '004c34f7')
        self.assertEqual(report['restore_rows'], [{'site': '004c34f7', 'status': 'restore_failed', 'found': 'c8000000', 'registered': True}])
        self.assertEqual(report['result'], {'checks': 2, 'failures': 1})


if __name__ == '__main__':
    unittest.main()
