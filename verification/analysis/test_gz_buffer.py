"""Host tests of the gz read-ahead buffer runner's report parser and of the recorded run.

The buffer itself (src/proxy/gz_buffer.cpp) is verified against the game's real
zlib1.dll by the Wine fixture (verification/probe/gz_buffer_fixture.cpp through
run_gz_buffer.py). These tests keep the runner's acceptance rules honest on
synthetic reports and re-check every recorded summary in verification/results.
"""
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification' / 'probe'))
import run_gz_buffer as runner  # noqa: E402


def synthetic(failures=0, drop_case=None, fail_line=False, terminal=True):
    lines = ['GZ_ZLIB version=1.2.3 exports=1 rewind=1']
    for name in runner.EXPECTED_CASES:
        if name == drop_case:
            continue
        lines.append(f'GZ_CASE name={name} capacity=4096 ops=100 checks=200 failures=0')
    if fail_line:
        lines.append('FAIL read_result big len=3 buffered=2 reference=3')
    lines.append('GZ_STATS opens=5 buffered=5 passthrough=1 closes=5 calls=10 small=9 served=30 real_reads=2 real_bytes=30 direct=0 getcs=1 tells=1 seeks=1 seeks_served=1 seeks_real=0 errors=0')
    lines.append('GZ_TIMING mode=unbuffered calls=1000 bytes=3000 seconds=0.600 ns_per_call=600.0 wall_ms=600')
    lines.append('GZ_TIMING mode=hooked calls=1000 bytes=3000 seconds=6.000 ns_per_call=6000.0 wall_ms=6000')
    lines.append('GZ_TIMING mode=buffered calls=1000 bytes=3000 seconds=0.050 ns_per_call=50.0')
    lines.append(f'GZ BUFFER RESULT checks={len(runner.EXPECTED_CASES) * 200} failures={failures}')
    if not terminal:
        lines.append('trailing')
    return '\n'.join(lines) + '\n'


class ParserTests(unittest.TestCase):
    def test_accepts_complete_report(self):
        report = runner.parse_report(synthetic())
        self.assertEqual(report['zlib_version'], '1.2.3')
        self.assertEqual(len(report['cases']), len(runner.EXPECTED_CASES))
        self.assertAlmostEqual(report['speedup'], 12.0)
        self.assertAlmostEqual(report['hook_envelope_ratio'], 10.0)
        self.assertEqual(report['timing']['buffered']['wall_ms'], None)
        self.assertEqual(report['statistics']['served'], 30)

    def test_rejects_failures_missing_cases_fail_lines_and_nonterminal_result(self):
        for bad in (synthetic(failures=1), synthetic(drop_case='timing'), synthetic(fail_line=True), synthetic(terminal=False), ''):
            with self.assertRaises(AssertionError):
                runner.parse_report(bad)

    def test_case_inventory_matches_fixture_source(self):
        source = (ROOT / 'verification/probe/gz_buffer_fixture.cpp').read_text()
        for name in runner.EXPECTED_CASES:
            self.assertIn(f'"{name}"', source, name)


class RecordedRunTests(unittest.TestCase):
    def summaries(self):
        return sorted((ROOT / 'verification/results').glob('**/gz-buffer-summary.json'))

    def test_recorded_summaries_pass_and_are_reparsable(self):
        for summary in self.summaries():
            with self.subTest(summary=str(summary.relative_to(ROOT))):
                data = json.loads(summary.read_text())
                self.assertTrue(data['passed'], data.get('error'))
                self.assertEqual(data['report']['zlib_version'], '1.2.3')
                self.assertEqual(data['zlib1_sha256_before'], data['zlib1_sha256_copied'])
                self.assertGreater(data['report']['hook_envelope_ratio'], 1.0)
                self.assertLess(data['report']['timing']['buffered']['ns_per_call'], data['report']['timing']['hooked']['ns_per_call'])
                text = (summary.parent / 'gz-buffer-fixture.txt').read_text(errors='replace')
                self.assertEqual(runner.parse_report(text)['checks'], data['report']['checks'])


if __name__ == '__main__':
    unittest.main()
