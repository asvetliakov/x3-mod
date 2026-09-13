"""Host controls for fixture serialization and numerical verdicts, without Wine."""
import importlib.util
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
spec = importlib.util.spec_from_file_location('bloom_gpu_runner', ROOT / 'verification/probe/run_bloom_filter.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class BloomFixtureTests(unittest.TestCase):
    def test_bundle_preserves_fp16_inputs_and_float32_parameters(self):
        cases = runner.make_cases()
        with tempfile.TemporaryDirectory(prefix='x3-bloom-bundle-') as directory:
            path = Path(directory) / 'cases.bin'
            runner.write_bundle(cases, path)
            data = path.read_bytes()
        self.assertEqual(data[:8], b'X3BLM001')
        self.assertEqual(struct.unpack_from('<I', data, 8)[0], len(cases))
        offset = 12
        names = set()
        for case in cases:
            names.add(case['name'])
            w, h, levels, mode, strength, threshold, knee, scatter, exposure, clamp = struct.unpack_from('<4I6f', data, offset)
            offset += 40
            self.assertEqual((w, h), (len(case['image'][0]), len(case['image'])))
            self.assertEqual((levels, mode), (case['params'].levels, runner.ref.DECODE_MODES.index(case['mode'])))
            self.assertEqual((strength, threshold, knee, scatter), tuple(getattr(case['params'], k)
                             for k in ('strength', 'threshold', 'knee', 'scatter')))
            self.assertEqual((exposure, clamp), (case['exposure'], case['clamp']))
            for row in case['image']:
                for pixel in row:
                    actual = struct.unpack_from('<4e', data, offset)
                    offset += 8
                    for a, b in zip(actual, pixel):
                        self.assertTrue(math.isnan(a) and math.isnan(b) or a == b)
        self.assertEqual(offset, len(data))
        self.assertEqual(len(names), len(cases))
        self.assertTrue({'geometry_82', 'geometry_8462', 'geometry_15611'} <= names)

    def test_oracle_quantizes_each_store_and_stages_are_complete(self):
        case = next(c for c in runner.make_cases() if c['name'] == 'random_odd_chroma')
        stages = runner.reference_stages(case)
        levels = len(runner.ref.layout(len(case['image'][0]), len(case['image']), case['params'].levels))
        self.assertEqual(set(stages), {f'd{i}' for i in range(levels)} |
                         {f'u{i}' for i in range(levels - 1)} | {'final'})
        for image in stages.values():
            for row in image:
                for pixel in row:
                    self.assertEqual(pixel, tuple(runner.half(c) for c in pixel))
        self.assertEqual(len(stages['final']), len(case['image']))
        self.assertEqual(len(stages['final'][0]), len(case['image'][0]))
        one = next(c for c in runner.make_cases() if c['name'] == 'sampling_1px_hdr')
        self.assertEqual(runner.reference_stages(one), {'d0': [[(16., 4., .5)]], 'final': [[(16., 4., .5)]]})

    def test_verdict_accepts_exact_reference_rejects_corruption(self):
        expected = [[(0., 1., 8.), (16., 4., .5)]]
        with tempfile.TemporaryDirectory(prefix='x3-bloom-verdict-') as directory:
            path = Path(directory) / 'readback.rgba16f'
            def check(pixels):
                path.write_bytes(b''.join(struct.pack('<4e', *p) for p in pixels))
                return runner.compare_image(path, expected)
            valid = [(0., 1., 8., 0.), (16., 4., .5, 0.)]
            result = check(valid)
            self.assertTrue(result['passed'])
            self.assertEqual((result['channels'], result['max_absolute_error']), (6, 0))
            for bad in ((.1, 1., 8., 0.), (math.nan, 1., 8., 0.),
                        (math.inf, 1., 8., 0.), (-.01, 1., 8., 0.), (0., 1., 8., 1.)):
                self.assertFalse(check([bad, valid[1]])['passed'])
            self.assertFalse(check(list(reversed(valid)))['passed'])
            path.write_bytes(b'bad')
            with self.assertRaises(ValueError):
                runner.compare_image(path, expected)
            path.write_bytes(struct.pack('<4e', .00001, 0., 0., 0.))
            self.assertFalse(runner.compare_image(path, [[(0., 0., 0.)]], require_exact_black=True)['passed'])

    def test_nonfinite_scope_and_serializable_metadata(self):
        import json
        cases = runner.make_cases()
        metadata = [runner.case_metadata(c) for c in cases]
        json.dumps(metadata, allow_nan=False)
        for case in cases:
            if case['group'] == 'nonfinite_extraction':
                for image in runner.reference_stages(case).values():
                    self.assertTrue(all(math.isfinite(v) and 0 <= v <= runner.ref.FP16_MAX
                                        for row in image for pixel in row for v in pixel))


if __name__ == '__main__':
    unittest.main()
