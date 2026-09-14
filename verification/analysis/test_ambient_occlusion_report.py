"""The ambient occlusion runner's parser on a fixture transcript excerpt.

Exercises run_ambient_occlusion.parse: check tallies, the reference fraction,
oracle verdicts, the apply counts, the timing budget flags and the variant
lines. No Wine, no build.
"""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'probe'))
import run_ambient_occlusion as runner  # noqa: E402

SAMPLE = '''CAPS ps=ffff0300 vs=fffe0300 ps30_slots=512 rts=4 src_zero=1 dest_srccolor=1
CHECK twin_ps_2_0 PASS
ATTACH enabled=1 largest_program_slots=483 references=6
CHECK attach_enabled PASS
REFERENCE scene=plane width=1280 height=768 pixels=245760 sentinel=0 ones=245760 mean_abs=0.000000 p999=0.000000 max=0.000000 within_002=245760 half_depth_max_rel=6.885e-07
APPLY scene=plane channels=2949120 exact=2949120 one_ulp=0 over=0 max_ulp=0 exact_truncate=2949120 alpha_changed=0
ORACLE scene=plane label=flat_identity covered=245760 not_one=0 below_0999=0 PASS
ORACLE scene=sphere label=contact_ring mean=0.9569 min=0.8774 pixels=62 FAIL
CHECK oracle_sphere_contact_ring FAIL
CHECK attach_enabled PASS
FP16_STORE value0=0.99987793 stored0=3c00 value1=1.00073242 stored1=3c02 mode=round_to_nearest
TIMING_QUADS width=1280 height=768 linearize_ms=0.0500 gtao_ms=0.3000 blur1_ms=0.1000 blur2_ms=0.1000 apply_ms=0.1500 sum_ms=0.7000 samples=6
RESET PASS references=13 allocations=2
TIMING width=1280 height=768 fenced_on_ms=0.9109 fenced_off_ms=0.0146 submit_ms=0.1366 chain_ms=0.4963 gpu_ms=0.3597 samples=7
TIMING_VARIANT width=1280 height=768 variant=no_blur fenced_on_ms=0.8091 chain_ms=0.7945 samples=4
TIMING width=1920 height=1080 fenced_on_ms=1.4138 fenced_off_ms=0.0143 submit_ms=0.1106 chain_ms=1.3995 gpu_ms=1.2889 samples=7
RESULT FAIL checks=4 failures=1
'''


class AmbientOcclusionReportTests(unittest.TestCase):
    def test_parse(self):
        report = runner.parse(SAMPLE)
        self.assertEqual((report['check_count'], report['check_failures']), (4, 1))  # the repeated label counts twice
        self.assertEqual(report['checks'], [['twin_ps_2_0', True], ['attach_enabled', True], ['oracle_sphere_contact_ring', False], ['attach_enabled', True]])
        self.assertEqual(report['failed_checks'], ['oracle_sphere_contact_ring'])
        self.assertEqual(report['fp16_store']['mode'], 'round_to_nearest')
        self.assertEqual(report['timing_quads'][0]['gtao_ms'], 0.3)
        self.assertEqual(report['reference']['plane']['within_002_fraction'], 1.0)
        self.assertEqual(report['reference']['plane']['pixels'], 245760)
        self.assertEqual([o['passed'] for o in report['oracles']], [True, False])
        self.assertEqual(report['oracles'][1]['mean'], 0.9569)
        self.assertEqual(report['apply']['plane']['exact_truncate'], 2949120)
        self.assertEqual(report['attach']['largest_program_slots'], 483)
        self.assertEqual(report['reset'], {'references': 13, 'allocations': 2})
        self.assertEqual([(t['width'], t['budget']['within_acceptance'], t['budget']['within_cap']) for t in report['timing']],
                         [(1280, True, True), (1920, False, False)])
        self.assertEqual(report['timing_variants'][0]['variant'], 'no_blur')
        self.assertEqual(report['result'], {'checks': 4, 'failures': 1, 'verdict': 'FAIL'})

    def test_budget_constants_follow_the_note(self):
        self.assertEqual((runner.BUDGET_MS['acceptance'], runner.BUDGET_MS['cap']), (0.5, 0.8))
        self.assertEqual(runner.REFERENCE_TOLERANCE, {'mean_abs': 0.002, 'within_002_fraction': 0.999})


if __name__ == '__main__':
    unittest.main()
