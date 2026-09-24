"""Terran-station LOD patch Wine fixture record (run_terran_lod_patch.py): strict JSON, bottle, pass counts,
executed branches, install/restore rows, protections and the build audit."""
import json
import subprocess
import unittest
from collections import Counter
from pathlib import Path

import run_terran_lod_patch as runner

ROOT = Path(__file__).resolve().parents[2]
RECORD = ROOT / 'verification/results/bottle-X3/terran-lod-patch.json'
CHECKS = 92
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


class TerranLodPatchRecordTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.record = json.loads(RECORD.read_text(), parse_constant=strict)

    def branch(self, memory, step):
        rows = [b for b in self.record['branches'] if (b['memory'], b['step']) == (memory, step)]
        self.assertEqual(len(rows), 1, (memory, step))
        return rows[0]['flag31'], rows[0]['clear']

    def test_run_and_bottle(self):
        r = self.record
        self.assertEqual(record_problems(r), [])
        self.assertFalse(r['game_launched'])
        self.assertEqual(r['bottle']['environment']['FEX_X87REDUCEDPRECISION'], '1')
        self.assertEqual(r['bottle']['environment']['WINEMSYNC'], '1')
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
        self.assertTrue(mutated(lambda r: r['bottle']['environment'].pop('FEX_X87REDUCEDPRECISION')))

    def test_bound_to_its_source_commit(self):
        source = self.record['source']
        self.assertRegex(source['commit'], r'^[0-9a-f]{40}$')
        self.assertFalse(source['production_sources_dirty'], source['dirty_paths'])
        self.assertEqual(self.record['production_sources'], list(runner.PRODUCTION_SOURCES))
        # The record stays valid only while the linked production sources equal the recorded commit's.
        try:
            known = subprocess.run(['git', '-C', str(ROOT), 'cat-file', '-e', source['commit'] + '^{commit}'], capture_output=True).returncode == 0
        except OSError:
            known = False
        if not known:
            self.skipTest('recorded commit not in this checkout')
        diff = subprocess.run(['git', '-C', str(ROOT), 'diff', '--name-only', source['commit'], '--', *runner.PRODUCTION_SOURCES],
                              capture_output=True, text=True, check=True).stdout.split()
        self.assertEqual(diff, [], 'production sources changed since the record: rerun run_terran_lod_patch.py')

    def test_pass_counts(self):
        r = self.record
        self.assertEqual((r['check_count'], r['pass_count'], len(r['checks'])), (CHECKS, CHECKS, CHECKS))
        self.assertEqual(r['result'], {'checks': CHECKS, 'failures': 0})
        self.assertTrue(all(c['pass'] for c in r['checks']))
        names = [c['name'] for c in r['checks']]
        self.assertEqual(len(names), len(set(names)))
        for name in ('engine_page_at_engine_va', 'engine_window_at_window_va_is_expected_window', 'engine_site_offset_4_of_its_qword',
                     'install_ok_atomic_row', 'install_sequence_counts', 'install_readback_eb05_rest_of_page_unchanged', 'restore_row',
                     'rollback_readback_rolled_back', 'rollback_dropped_write_rolled_back', 'rollback_unprotected_original_bytes_page_writable',
                     'rollback_failed_registered', 'restore_dropped_write_failed_registered', 'restore_not_owned_7405', 'refuse_protect_failed_untouched',
                     'refuse_changed_window_untouched', 'refuse_already_patched_window_untouched', 'roview_raise_refused_by_os',
                     'roview_install_protect_failed', 'private_install_ok_atomic_readback', 'late_initialize_refused', 'late_install_at_refused',
                     'install_lasterror_preserved', 'restore_lasterror_preserved'):
            self.assertIn(name, names)

    def test_executed_branches(self):
        # flag31 = 1: the je fell through and stored the flag; 0: the jump skipped it; the control node is 0 throughout.
        for memory, step, expected in (('engine', 'before', (1, 0)), ('engine', 'after_patch', (0, 0)), ('engine', 'after_restore', (1, 0)),
                                       ('engine', 'after_rollback', (1, 0)), ('engine', 'after_rollback_unprotected', (1, 0)),
                                       ('engine', 'after_restore_not_owned', (1, 0)), ('engine', 'after_protect_failed', (1, 0)),
                                       ('engine', 'after_patch_flushed', (0, 0)), ('engine', 'after_restore_flushed', (1, 0)),
                                       ('engine', 'after_late', (1, 0)), ('private', 'before', (1, 0)), ('private', 'after_patch', (0, 0)),
                                       ('private', 'after_restore', (1, 0))):
            self.assertEqual(self.branch(memory, step), expected, (memory, step))
        self.assertEqual(self.branch('engine', 'after_rollback_failed'), (None, None))  # eb 06 is not executed
        self.assertEqual(set(self.record['flush']), {'patch_without_flush_flag31', 'restore_without_flush_flag31', 'raw_patch_flag31', 'raw_restore_flag31'})

    def test_log_rows(self):
        installs = self.record['install_rows']
        self.assertTrue(all(row['site'] == '0047d01c' for row in installs))
        self.assertEqual(Counter((row['status'], row['reason'], row['write']) for row in installs), Counter({
            ('patched', 'ok', 'atomic'): 4, ('refused', 'bytes_mismatch', 'none'): 2, ('refused', 'patch_rolled_back', 'atomic'): 2,
            ('off', 'distance', 'none'): 1, ('refused', 'invalid_setting', 'none'): 1, ('refused', 'too_long', 'none'): 1,
            ('refused', 'executable_mismatch', 'none'): 1, ('refused', 'protect_failed', 'none'): 1, ('refused', 'rollback_unprotected', 'atomic'): 1,
            ('patched_unverified', 'rollback_failed', 'atomic'): 1, ('refused', 'late_claim', 'none'): 1}))
        restores = self.record['restore_rows']
        self.assertEqual(Counter((row['status'], row['found'], row['registered']) for row in restores), Counter({
            ('restored', 'eb05', False): 3, ('restore_failed', 'eb06', True): 2, ('restore_not_owned', 'eb06', False): 1,
            ('restored', '7405', False): 1, ('restore_not_owned', None, False): 1}))

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
        roview = self.record['roview']
        self.assertEqual(roview['raise'], 0)
        self.assertNotEqual(roview['error'], 0)

    def test_build_audit(self):
        audit = self.record['build_audit']
        self.assertEqual(audit['module_imports_own_protect_or_flush'], [])
        self.assertEqual(len(audit['module_calls_seam']), 4)
        self.assertGreaterEqual(audit['engine_patch_lock_cmpxchg8b'], 1)
        self.assertEqual((audit['engine_page_va'], audit['x3mlod']['va'], audit['x3mlod']['size']), ('0047d000', '0047d000', 4096))
        self.assertEqual(audit['overlapping_sections'], [])


class TerranLodPatchParserTests(unittest.TestCase):
    def test_parse(self):
        stdout = '\n'.join((
            'CHECK pass install_ok_atomic_row', 'CHECK FAIL restore_row detail text',
            'BRANCH memory=engine step=after_patch flag31=0 clear=0',
            'BRANCH memory=engine step=after_rollback_failed flag31=- clear=- (eb 06 not executed: mid-instruction target)',
            'PROTECT memory=engine step=install previous=0x20 during=0x80 after=0x20', 'ROVIEW protect=0x20 raise=0 error=87',
            'FLUSH patch_without_flush_flag31=0 (0 = new jmp executed) restore_without_flush_flag31=1 (1 = je executed)',
            'FLUSH_RAW patch_flag31=0 restore_flag31=1', 'TIMING install_us=226.4 restore_us=184.3',
            'LOG terran_station_lod site=0047d01c status=patched reason=ok mode=size setting=- write=atomic',
            'LOG terran_station_lod_restore site=0047d01c status=restore_failed found=eb06 registered=1', 'RESULT checks=2 failures=1'))
        report = runner.parse(stdout)
        self.assertEqual(report['checks'], [{'name': 'install_ok_atomic_row', 'pass': True}, {'name': 'restore_row', 'pass': False, 'detail': 'detail text'}])
        self.assertEqual([(b['flag31'], b['clear']) for b in report['branches']], [(0, 0), (None, None)])
        self.assertEqual(report['flush'], {'patch_without_flush_flag31': 0, 'restore_without_flush_flag31': 1, 'raw_patch_flag31': 0, 'raw_restore_flag31': 1})
        self.assertEqual(report['roview'], {'protect': '0x20', 'raise': 0, 'error': 87})
        self.assertEqual(report['install_rows'][0]['site'], '0047d01c')
        self.assertEqual(report['restore_rows'], [{'site': '0047d01c', 'status': 'restore_failed', 'found': 'eb06', 'registered': True}])
        self.assertEqual(report['result'], {'checks': 2, 'failures': 1})


if __name__ == '__main__':
    unittest.main()
