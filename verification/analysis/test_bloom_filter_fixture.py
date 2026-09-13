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

    def test_unquantized_intermediate_pipeline_is_not_the_store_oracle(self):
        case = next(c for c in runner.make_cases() if c['name'] == 'srgb_breakpoint')
        p = case['params']; image = case['image']
        current = [[runner.ref.prefilter(pixel, p, case['exposure'], case['clamp'], case['mode'])
                    for pixel in row] for row in image]
        chain = []
        for _ in runner.ref.layout(len(image[0]), len(image), p.levels):
            current = runner.ref.downsample(current)  # deliberately omit every intermediate store
            chain.append(current)
        for fine in reversed(chain[:-1]):
            coarse = runner.ref.tent(current, len(fine[0]), len(fine))
            current = [[tuple((1-p.scatter)*a+p.scatter*b for a,b in zip(pixel,other))
                        for pixel,other in zip(row,other_row)] for row,other_row in zip(fine,coarse)]
        wrong = runner.quantize(runner.ref.tent(current, len(image[0]), len(image)))
        self.assertNotEqual(wrong, runner.reference_stages(case)['final'])

    def test_characterization_rounding_and_heldout_independence(self):
        ch = runner.characterization
        self.assertEqual(ch.fp16(1.00075, 'toward_zero'), 1.)
        self.assertEqual(ch.fp16(-1.00075, 'toward_zero'), -1.)
        self.assertNotEqual(ch.fp16(1.00075), 1.)
        self.assertEqual(ch.fp16(2**-20, flush_subnormal=True), 0.)
        self.assertEqual(ch.fp16(16.0078125,'nearest_up'),16.015625)
        self.assertEqual(ch.fp16(16.0078125,'nearest_even'),16.)
        self.assertEqual(ch.fp16(-16.0078125,'nearest_up'),-16.)
        inputs = ch.make_inputs()
        model = dict(fraction_bits=8, fraction_rounding='nearest_even',
                     result_precision='nearest_even', flush_subnormal=False)
        case = next(c for c in runner.make_cases() if c['name'] == 'sampling_1px_hdr')
        with tempfile.TemporaryDirectory(prefix='x3-bloom-calibration-control-') as directory:
            path = Path(directory)
            def write(name, code, pixels):
                (path/name).write_bytes(b''.join(struct.pack('<4'+code,*p) for p in pixels))
            write('characterization_store.rgba16f', 'e',
                  [tuple(ch.fp16(v,'toward_zero') for v in p) for p in inputs['stores']])
            for i,pair in enumerate(inputs['endpoints']):
                write(f'characterization_point_{i}.rgba32f','f',pair)
                write(f'characterization_sample_{i}.rgba32f','f',
                      [ch.sample_pair(pair,uv[0],model) for uv in inputs['phases']])
            for i,quad in enumerate(inputs['endpoints2d']):
                write(f'characterization_point2d_{i}.rgba32f','f',quad)
                write(f'characterization_sample2d_{i}.rgba32f','f',
                      [ch.sample_quad(quad,uv[0],uv[1],model,'single') for uv in inputs['phases2d']])
            write('characterization_uv_1x1.rgba32f','f',[(.5,.5,0.,1.)])
            report = ch.analyze(path,[case],16384,16384)
            self.assertTrue(report['passed'], report)
            self.assertEqual(report['sampler_model'],model)
            self.assertEqual(report['store_model'],dict(rounding='toward_zero',flush_subnormal=False))
            # Corrupt only a held-out endpoint/phase. Calibration still picks
            # the same unique model; the final result must reject independently.
            sample = path/'characterization_sample_3.rgba32f'
            data = bytearray(sample.read_bytes())
            original=bytes(data)
            struct.pack_into('<f',data,0,.125)
            sample.write_bytes(data)
            rejected = ch.analyze(path,[case],16384,16384)
            self.assertFalse(rejected['passed'])
            self.assertEqual(rejected['sampler_candidates'],[model])
            self.assertGreater(rejected['sampler_heldout_failure_count'],0)
            sample.write_bytes(original)
            # The separate natural-precision 2D envelope also has a negative
            # control; an out-of-envelope held-out result must never tune it.
            sample2d=path/'characterization_sample2d_3.rgba32f'
            data=bytearray(sample2d.read_bytes())
            struct.pack_into('<f',data,0,.25)
            sample2d.write_bytes(data)
            rejected2d=ch.analyze(path,[case],16384,16384)
            self.assertFalse(rejected2d['passed'])
            self.assertGreater(rejected2d['sampler2d_heldout_failure_count'],0)

    def test_precision_analyzer_keeps_local_and_whole_chain_independent(self):
        import analyze_bloom_precision as precision
        model=dict(fraction_bits=8,fraction_rounding='nearest_up',result_precision='nearest_up',flush_subnormal=False)
        store=dict(rounding='toward_zero',flush_subnormal=False)
        case=next(c for c in runner.make_cases() if c['name']=='sampling_1px_hdr')
        uvs={(1,1):[(.5,.5,0.,1.)]}
        whole=precision.modeled_stages(case,uvs,model,store)
        self.assertEqual(whole['final'],[[(16.,4.,.5)]])
        actual={'d0':[[(8.,2.,.25)]],'final':whole['final']}
        local=precision.modeled_stages(case,uvs,model,store,actual)
        self.assertEqual(local['final'],[[(8.,2.,.25)]])
        self.assertFalse(precision.compare(local['final'],whole['final'])['passed'])
        with self.assertRaises(ValueError):
            precision.up_1d([[(1.,1.,1.)]],[[(1.,1.,1.)]],1.,[(1.5,.5,0.,1.)],model,store)

    def test_precision_qualification_recomputes_ideal_and_rejects_new_failures(self):
        import analyze_bloom_precision as precision
        from unittest.mock import patch
        one=next(c for c in runner.make_cases() if c['name']=='sampling_1px_hdr')
        two=dict(one,name='two_dimensional_control',image=one['image']*2)
        cases=[one,two]
        with tempfile.TemporaryDirectory(prefix='x3-bloom-current-ideal-') as directory:
            records={}
            for i,case in enumerate(cases):
                for stage,image in runner.reference_stages(case).items():
                    path=Path(directory)/f'{i}_{stage}.rgba16f'
                    path.write_bytes(b''.join(struct.pack('<4e',*pixel,0.)
                        for row in image for pixel in row))
                    # Historical flags are deliberately false for exact images.
                    records[0,i,stage]=dict(file=str(path),passed=False)
            verdicts,summary=precision.current_ideal_verdicts(cases,records,set())
            self.assertTrue(summary['required_checks_passed'])
            self.assertEqual(summary['images'],len(records))
            self.assertTrue(all(row['passed'] for row in verdicts.values()))
            for record in records.values():
                record['passed']=True
            original=precision.run.reference_stages
            def changed_oracle(case):
                stages=original(case)
                stages['final']=[[(0.,0.,0.) for pixel in row] for row in stages['final']]
                return stages
            # A current-oracle change must reject despite historical PASS;
            # even accidentally admitting a 2D case cannot hide its failure.
            with patch.object(precision.run,'reference_stages',side_effect=changed_oracle):
                _,summary=precision.current_ideal_verdicts(cases,records,{1})
            self.assertFalse(summary['required_checks_passed'])
            self.assertEqual(summary['failed_images'],2)
            self.assertEqual(summary['unmodeled_failed_images'],1)
            self.assertEqual(summary['two_dimensional_failed_images'],1)

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
