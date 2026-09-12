"""Host tests of the resource-reader runner's report parser and of the recorded run.

The reader core, the .dat handle pool and the engine-probe machinery are
verified against the game's real zlib1.dll by the Wine fixture
(verification/probe/resource_reader_fixture.cpp through run_resource_reader.py).
These tests keep the runner's acceptance rules honest on synthetic reports and
re-check every recorded summary in verification/results.
"""
import json
import hashlib
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification' / 'probe'))
import run_resource_reader as runner  # noqa: E402


def synthetic(failures=0, drop_case=None, fail_line=False, terminal=True, wrong_reason=False, mismatched=0):
    lines = ['RR_ZLIB version=1.2.3', 'RR_CASE name=fast loose_handled=10 record_handled=20 sources=11',
             'RR_CASE name=cursor sources=20 class_records=16 loose_and_record_handled=20 last_record_class=1']
    for case, reason in runner.EXPECTED_FALLBACKS.items():
        lines.append(f'RR_FALLBACK case={case} reason={"none" if wrong_reason and case == "method" else reason}')
    lines += ['RR_CASE name=fallbacks', 'RR_CASE name=probes installed=4',
              f'RR_STATS mode=verify calls=22 handled=20 fallbacks=2 verify_files=20 verify_equal={20 - mismatched} verify_mismatched={mismatched} bytes_in=100 bytes_out=1000 our_us=10.5 original_us=99.0',
              'RR_TIMING name=large bytes=3000000 rounds=5 fast_us=1200.0 reference_us=9000.0 ratio=7.50',
              'RR_STATS_CURSOR verify_files=40 cursor_short=32 verify_equal=60 verify_mismatched=1',
              'RR_TIMING name=medium bytes=31000 rounds=400 fast_us=120.0 reference_us=300.0 ratio=2.50',
              'RR_TIMING name=large_record bytes=3000000 rounds=5 fast_us=1300.0 reference_us=9100.0 ratio=7.00',
              'RR_PHASES name=large extent=1200000 read_us=100.0 scan_us=50.0 alloc_us=5.0 inflate_us=1000.0 total_us=1200.0',
              'RR_PHASES name=medium extent=18000 read_us=10.0 scan_us=5.0 alloc_us=1.0 inflate_us=100.0 total_us=120.0',
              'RR_PHASES name=large_record extent=1200000 read_us=100.0 scan_us=150.0 alloc_us=5.0 inflate_us=1000.0 total_us=1300.0',
              'RR_CASE name=pool opens=5 reused=2 real_opens=3 kept=4 real_closes=2']
    if drop_case:
        lines = [l for l in lines if not l.startswith(f'RR_CASE name={drop_case}')]
    if fail_line:
        lines.append('FAIL fast_bytes rr_small.pck size=1 expect=2')
    lines += ['RR_CASE name=rewind_failure modes=2 allocations_freed=2 entry_restored=2 bookkeeping_unchanged=2 original_retries=2',
              'RR_CASE name=error_abi incoming=826366246 original_input=826366246 outgoing=370672441 returned=370672441',
              'resource_reader verify equal=0 size=31000 original_size=31000 mismatches=1 first=0 original_null=0 globals_ok=1 counters_ok=1 cursor_ok=1 position_ok=1 cursor=19654 expected_cursor=19654 position=20113 expected_position=20113 catalogue=1 scrambled=1 our_us=350.000 original_us=400.000']
    lines.append(f'RESOURCE READER RESULT checks=300 failures={failures}')
    if not terminal:
        lines.append('trailing')
    return '\n'.join(lines) + '\n'


class ParserTests(unittest.TestCase):
    def test_accepts_complete_report(self):
        report = runner.parse_report(synthetic())
        self.assertEqual(report['zlib_version'], '1.2.3')
        self.assertEqual(set(report['cases']), set(runner.EXPECTED_CASES))
        self.assertEqual(report['cases']['fast']['record_handled'], 20)
        self.assertEqual(report['fallbacks']['transparent'], 'not_gzip')
        self.assertEqual(report['statistics']['verify_mismatched'], 0)
        self.assertAlmostEqual(report['timing']['large']['ratio'], 7.5)
        self.assertAlmostEqual(report['timing']['medium']['ratio'], 2.5)
        self.assertEqual(report['cases']['cursor']['class_records'], 16)
        self.assertEqual(report['cursor_statistics']['cursor_short'], 32)
        self.assertAlmostEqual(report['phases']['large_record']['inflate_us'], 1000.0)
        self.assertEqual(report['checks'], 300)

    def test_rejects_failures_missing_cases_fail_lines_wrong_reasons_and_mismatches(self):
        for bad in (synthetic(failures=1), synthetic(drop_case='pool'), synthetic(fail_line=True), synthetic(terminal=False),
                    synthetic(wrong_reason=True), synthetic(mismatched=1), ''):
            with self.assertRaises(AssertionError):
                runner.parse_report(bad)

    def test_rejects_duplicate_records_and_scalar_keys(self):
        base = synthetic()
        for prefix in ('RR_CASE name=cursor', 'RR_FALLBACK case=alloc', 'RR_STATS mode=',
                       'RR_STATS_CURSOR ', 'RR_TIMING name=large ', 'RR_PHASES name=medium ',
                       'RR_ZLIB ', 'resource_reader verify '):
            line = next(line for line in base.splitlines() if line.startswith(prefix))
            with self.subTest(prefix=prefix), self.assertRaises(AssertionError):
                runner.parse_report(base.replace(line, line + '\n' + line))
        for changed in ('class_records=0 class_records=16', 'class_records=16 class_records=0'):
            with self.assertRaises(AssertionError):
                runner.parse_report(base.replace('class_records=16', changed))

    def test_rejects_incomplete_or_wrong_cursor_witnesses(self):
        for original, changed in [('verify_files=40 ', ''), ('verify_equal=60 ', ''),
                                  ('verify_files=40', 'verify_files=39'), ('sources=20', 'sources=19'),
                                  ('cursor_short=32', 'cursor_short=31')]:
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                runner.parse_report(synthetic().replace(original, changed))

    def test_rejects_rewind_error_and_misaligned_diagnostic_witnesses(self):
        for original, changed in [('allocations_freed=2', 'allocations_freed=1'),
                ('entry_restored=2', 'entry_restored=0'), ('bookkeeping_unchanged=2', 'bookkeeping_unchanged=1'),
                ('original_input=826366246', 'original_input=65535'), ('returned=370672441', 'returned=0'),
                ('cursor_ok=1 ', ''), ('cursor=19654 ', 'cursor=1 '), ('catalogue=1', 'catalogue=20113'),
                ('our_us=350.000', 'our_us=nan'), ('position_ok=1', 'position_ok=0')]:
            base = synthetic()
            self.assertIn(original, base)
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                runner.parse_report(base.replace(original, changed))

    def test_rejects_incomplete_fast_probe_and_mode_records(self):
        for original, changed in [('loose_handled=10 record_handled=20 sources=11', ''),
                                  ('installed=4', 'installed=0'), ('mode=verify', 'mode=native')]:
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                runner.parse_report(synthetic().replace(original, changed))

    def test_exact_full_terminal_inventory(self):
        full = synthetic().replace('checks=300', 'checks=4721')
        self.assertEqual(runner.parse_report(full, expected_checks=4721)['checks'], 4721)
        for count in (0, 1, 4720, 4722):
            with self.subTest(count=count), self.assertRaises(AssertionError):
                runner.parse_report(full.replace('checks=4721', f'checks={count}'), expected_checks=4721)

    def test_rejects_repeated_terminal_and_empty_inventory(self):
        base = synthetic()
        with self.assertRaises(AssertionError):
            runner.parse_report(base + base.splitlines()[-1] + '\n')
        with self.assertRaises(AssertionError):
            runner.parse_report(base.replace('checks=300', 'checks=0'))

    def test_fallback_inventory_matches_fixture_source(self):
        source = (ROOT / 'verification/probe/resource_reader_fixture.cpp').read_text()
        for case in runner.EXPECTED_FALLBACKS:
            self.assertIn(f'"{case}"', source, case)
        core = (ROOT / 'src/proxy/resource_reader_core.cpp').read_text()
        for reason in set(runner.EXPECTED_FALLBACKS.values()):
            self.assertIn(f'"{reason}"', core, reason)


class RecordedRunTests(unittest.TestCase):
    def summaries(self):
        return sorted((ROOT / 'verification/results').glob('**/resource-reader-summary.json'))

    def test_recorded_summaries_pass_and_are_reparsable(self):
        for summary in self.summaries():
            with self.subTest(summary=str(summary.relative_to(ROOT))):
                data = json.loads(summary.read_text())
                self.assertTrue(data['passed'], data.get('error'))
                self.assertEqual(data['report']['zlib_version'], '1.2.3')
                self.assertEqual(data['zlib1_sha256_before'], data['zlib1_sha256_copied'])
                self.assertGreater(data['report']['timing']['large']['ratio'], 1.0)
                self.assertGreater(data['report']['timing']['medium']['ratio'], 1.0)
                text = (summary.parent / 'resource-reader-fixture.txt').read_text(errors='replace')
                if data.get('verification_schema') == 2:
                    self.assertEqual(runner.parse_report(text, expected_checks=data['expected_checks']), data['report'])
                    self.assertEqual(hashlib.sha256((summary.parent / 'resource-reader-fixture.txt').read_bytes()).hexdigest(), data['stdout_sha256'])
                    self.assertEqual(data['sources'], data['sources_after'])
                    self.assertEqual(data['zlib1_sha256_copied'], data['zlib1_sha256_copied_after'])
                else:
                    # Historical pre-review31 evidence is not accepted by the
                    # stricter current parser or relabelled as post-fix proof.
                    self.assertEqual(data['report']['checks'], 4707)
                    self.assertTrue(text.rstrip().endswith('RESOURCE READER RESULT checks=4707 failures=0'))


if __name__ == '__main__':
    unittest.main()
