"""Independent D24 numerical oracle: no Wine, GPU, fixture values or shader compiler.

Both exact-normalized D24 and float32-normalized comparison models are covered.
The latter demonstrates an IEEE midpoint endpoint collision that the currently
installed GPU's sampled behavior did not expose. This is sampled, not exhaustive.
"""
import hashlib
import json
from pathlib import Path
import random
import struct
import sys
import unittest

D24_MAX = (1 << 24) - 1


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def search(code, rounded_comparison, clamp_interior):
    """Explicit float32 arithmetic; comparisons return exact binary decisions."""
    stored = f32(code / D24_MAX)
    def passes(reference):
        # An f32 reference times a 24-bit integer fits exactly in float64 here.
        return reference <= stored if rounded_comparison else reference * D24_MAX <= code
    lower, upper = 0.0, 1.0
    for _ in range(24):
        reference = f32(f32(lower + upper) * 0.5)
        if passes(reference):
            lower = reference
        else:
            upper = reference
    midpoint = f32(f32(lower + upper) * 0.5)
    if clamp_interior:
        midpoint = min(max(midpoint, 2**-25), 1 - 2**-24)
    if passes(1.0):
        midpoint = 1.0
    if not passes(f32(0.5 / D24_MAX)):
        midpoint = 0.0
    return midpoint


def holdout_codes():
    # Different seed and distribution from the 22 GPU random tiles. Include
    # exponent transitions and neighboring codes, plus both endpoint clusters.
    codes = set(range(33)) | set(range(D24_MAX - 32, D24_MAX + 1))
    for bit in range(24):
        codes.update(x for x in range((1 << bit) - 2, (1 << bit) + 3) if 0 <= x <= D24_MAX)
    generator = random.Random(0xC0DEC024)
    codes.update(generator.randrange(D24_MAX + 1) for _ in range(8192))
    return sorted(codes)


def evaluate():
    cases = holdout_codes()
    report = dict(sampled_codes=len(cases), comparator_models=[], passed=True)
    for rounded in (False, True):
        maximum = 0.0
        failures = []
        for code in cases:
            actual = search(code, rounded, True)
            error = abs(actual * D24_MAX - code)
            maximum = max(maximum, error)
            sentinels = ((actual == 0) == (code == 0) and
                         (actual == 1) == (code == D24_MAX))
            if error > 2 or not sentinels:
                failures.append(dict(code=code, actual=actual, error_d24_lsb=error))
        report['comparator_models'].append(dict(
            model='float32_normalized' if rounded else 'exact_normalized',
            maximum_error_d24_lsb=maximum, failures=failures))
        report['passed'] &= not failures
    code = D24_MAX - 1
    report['old_endpoint_collision'] = dict(
        code=code, exact_normalized_depth=code / D24_MAX,
        old_float32_comparison_output=search(code, True, False),
        corrected_float32_comparison_output=search(code, True, True))
    report['passed'] &= (report['old_endpoint_collision']['old_float32_comparison_output'] == 1 and
                         report['old_endpoint_collision']['corrected_float32_comparison_output'] < 1)
    return report


class DepthDecodeOracleTests(unittest.TestCase):
    def test_old_float32_midpoint_collides_with_clear_sentinel(self):
        self.assertEqual(search(D24_MAX - 1, True, False), 1.0)
        self.assertEqual(search(D24_MAX - 1, True, True), 1 - 2**-24)

    def test_exact_endpoints_and_adjacent_codes(self):
        for rounded in (False, True):
            self.assertEqual(search(0, rounded, True), 0.0)
            self.assertEqual(search(D24_MAX, rounded, True), 1.0)
            for code in (1, 2, D24_MAX - 2, D24_MAX - 1):
                self.assertGreater(search(code, rounded, True), 0.0)
                self.assertLess(search(code, rounded, True), 1.0)

    def test_independent_holdout_analytic_error_and_sentinels(self):
        self.assertTrue(evaluate()['passed'])


if __name__ == '__main__':
    if sys.argv[1:] == ['--report']:
        report = evaluate()
        root = Path(__file__).resolve().parents[2]
        report['sources_sha256'] = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
                                   for p in (Path(__file__), root / 'src/temporal/depth_decode.hlsl')}
        destination = root / 'verification/results/depth-decode-oracle.json'
        destination.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2))
        raise SystemExit(0 if report['passed'] else 1)
    unittest.main()
