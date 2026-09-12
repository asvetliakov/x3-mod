"""Host tests of the CryptoAPI context/key cache runner's report parser and of the recorded runs.

The cache itself (src/proxy/crypt_cache.cpp) is verified against the bottle's real
ADVAPI32/rsaenh by the Wine fixture (verification/probe/crypt_cache_fixture.cpp
through run_crypt_cache.py). These tests keep the runner's acceptance rules honest
on synthetic reports and re-check every recorded summary in verification/results.
"""
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification' / 'probe'))
import run_crypt_cache as runner  # noqa: E402


def synthetic(iterations=200, failures=0, mismatches=0, on_acquires=2, leak=0, container_absent=1, on_mean=120.0, fail_line=False, terminal=True, drop=None):
    verified = iterations - iterations // 4  # every fourth signature is corrupted or truncated
    lines = [f'CRYPT_KEY bits=2048 blob_bytes=276 signature_bytes=256 expected_verified={verified} provider="Microsoft Base Cryptographic Provider v1.0" container=X3mCryptCacheFixture',
             f'CRYPT_MODE mode=off iterations={iterations} verified={verified} rejected={iterations - verified} real_acquires={3 * iterations} real_acquire_ok={iterations} real_deletes={2 * iterations} real_delete_ok={iterations} real_releases={iterations} real_imports={iterations} real_destroys={iterations} total_ms=2600.000 mean_us=13000.000 median_us=12900.000 min_us=12000.000 max_us=15000.000 context_mean_us=12500.000',
             f'CRYPT_MODE mode=on iterations={iterations} verified={verified} rejected={iterations - verified} real_acquires={on_acquires} real_acquire_ok=1 real_deletes=1 real_delete_ok=0 real_releases=0 real_imports=1 real_destroys=0 total_ms=24.000 mean_us={on_mean:.3f} median_us=110.000 min_us=100.000 max_us=13000.000 context_mean_us=2.000',
             f'CRYPT_COMPARE iterations={iterations} steps=12 mismatches={mismatches} verified_off={verified} verified_on={verified} expected_verified={verified}',
             f'CRYPT_STATS acquires={3 * iterations} hits={iterations - 1} misses=1 failed_passthrough=1 releases_suppressed={iterations} imports={iterations} import_hits={iterations - 1} deletes_emulated={2 * iterations - 1} deletes_passthrough=1 busy_passthrough=0 evictions=0 releases={iterations} import_passthrough=0 destroys={iterations} destroys_suppressed={iterations} providers_cached=1 keys_cached=1 probe_error=0x80090016',
             f'CRYPT_SHUTDOWN released=1 real_acquires=1 real_releases=1 real_destroys=1 container_absent={container_absent}',
             f'CRYPT_LEAK live_providers={leak} live_keys=0']
    if fail_line:
        lines.append('FAIL compare iteration=3 step=verify result off=1 on=0')
    lines = [l for l in lines if not (drop and l.startswith(drop))]
    lines.append(f'CRYPT CACHE RESULT checks=40 failures={failures}')
    if not terminal:
        lines.append('trailing')
    return '\n'.join(lines) + '\n'


class ParserTests(unittest.TestCase):
    def test_accepts_complete_report(self):
        report = runner.parse_report(synthetic())
        self.assertEqual(report['key']['blob_bytes'], 276)
        self.assertEqual(report['modes']['off']['real_acquires'], 600)
        self.assertEqual(report['modes']['on']['real_acquires'], 2)
        self.assertEqual(report['statistics']['probe_error'], 0x80090016)
        self.assertEqual(report['compare']['mismatches'], 0)
        self.assertAlmostEqual(report['speedup'], 13000.0 / 120.0)
        self.assertAlmostEqual(report['saved_us_per_check'], 12880.0)
        self.assertAlmostEqual(report['context_saved_us_per_check'], 12498.0)

    def test_rejects_bad_reports(self):
        bad = (synthetic(failures=1), synthetic(mismatches=1), synthetic(on_acquires=3), synthetic(leak=1), synthetic(container_absent=0),
               synthetic(on_mean=20000.0), synthetic(fail_line=True), synthetic(terminal=False), synthetic(drop='CRYPT_STATS'),
               synthetic(drop='CRYPT_KEY'), synthetic(drop='CRYPT_LEAK'), '')
        for text in bad:
            with self.assertRaises(AssertionError):
                runner.parse_report(text)

    def test_step_inventory_matches_fixture_source(self):
        source = (ROOT / 'verification/probe/crypt_cache_fixture.cpp').read_text()
        for step in ('delete_before', 'new_keyset', 'import_key', 'create_hash', 'hash_data', 'verify', 'hash_size', 'hash_value',
                     'destroy_hash', 'destroy_key', 'release', 'delete_after'):
            self.assertIn(f'"{step}"', source, step)
        self.assertIn('X2EgosoftCSPContainer', (ROOT / 'src/proxy/crypt_cache.h').read_text())
        self.assertNotIn('X2EgosoftCSPContainer', source, 'the fixture must not touch the game\'s container')


class RecordedRunTests(unittest.TestCase):
    def summaries(self):
        return sorted((ROOT / 'verification/results').glob('**/crypt-cache-summary.json'))

    def test_recorded_summaries_pass_and_are_reparsable(self):
        summaries = self.summaries()
        self.assertTrue(summaries, 'no recorded crypt-cache-summary.json')
        for summary in summaries:
            with self.subTest(summary=str(summary.relative_to(ROOT))):
                data = json.loads(summary.read_text())
                self.assertTrue(data['passed'], data.get('error'))
                self.assertIn('wine_arch', data['bottle'])
                self.assertEqual(data['report']['key']['blob_bytes'], 276)
                self.assertGreater(data['report']['speedup'], 1.0)
                self.assertEqual(data['report']['leak'], {'live_providers': 0, 'live_keys': 0})
                text = (summary.parent / 'crypt-cache-fixture.txt').read_text(errors='replace')
                self.assertEqual(runner.parse_report(text)['checks'], data['report']['checks'])


if __name__ == '__main__':
    unittest.main()
