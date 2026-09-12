"""Host tests of the resource-reader runner's report parser and of the recorded run.

The reader core, the .dat handle pool and the engine-probe machinery are
verified against the game's real zlib1.dll by the Wine fixture
(verification/probe/resource_reader_fixture.cpp through run_resource_reader.py).
These tests keep the runner's acceptance rules honest on synthetic reports and
re-check every recorded summary in verification/results.
"""
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification' / 'probe'))
import run_resource_reader as runner  # noqa: E402


def synthetic(failures=0, drop_case=None, fail_line=False, terminal=True, wrong_reason=False, mismatched=0):
    lines = ['RR_ZLIB version=1.2.3', 'RR_CASE name=fast loose_handled=10 record_handled=20 sources=11']
    for case, reason in runner.EXPECTED_FALLBACKS.items():
        lines.append(f'RR_FALLBACK case={case} reason={"none" if wrong_reason and case == "method" else reason}')
    lines += ['RR_CASE name=fallbacks', 'RR_CASE name=probes installed=4',
              f'RR_STATS mode=verify calls=22 handled=20 fallbacks=2 verify_files=20 verify_equal={20 - mismatched} verify_mismatched={mismatched} bytes_in=100 bytes_out=1000 our_us=10.5 original_us=99.0',
              'RR_TIMING name=large bytes=3000000 rounds=5 fast_us=1200.0 reference_us=9000.0 ratio=7.50',
              'RR_TIMING name=medium bytes=31000 rounds=400 fast_us=120.0 reference_us=300.0 ratio=2.50',
              'RR_CASE name=pool opens=5 reused=2 real_opens=3 kept=4 real_closes=2']
    if drop_case:
        lines = [l for l in lines if not l.startswith(f'RR_CASE name={drop_case}')]
    if fail_line:
        lines.append('FAIL fast_bytes rr_small.pck size=1 expect=2')
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
        self.assertEqual(report['checks'], 300)

    def test_rejects_failures_missing_cases_fail_lines_wrong_reasons_and_mismatches(self):
        for bad in (synthetic(failures=1), synthetic(drop_case='pool'), synthetic(fail_line=True), synthetic(terminal=False),
                    synthetic(wrong_reason=True), synthetic(mismatched=1), ''):
            with self.assertRaises(AssertionError):
                runner.parse_report(bad)

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
                self.assertEqual(runner.parse_report(text)['checks'], data['report']['checks'])


if __name__ == '__main__':
    unittest.main()
