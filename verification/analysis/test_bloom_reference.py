"""Analytical filter controls, independent sampling invariants and CPU ABI."""
import dataclasses
import math
import random
from pathlib import Path
import shutil
import subprocess
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import bloom_reference as ref


def solid(w, h, rgb):
    return [[rgb for _ in range(w)] for _ in range(h)]


class BloomTests(unittest.TestCase):
    def assert_image_constant(self, image, expected, tolerance=1e-11):
        for row in image:
            for pixel in row:
                for actual, wanted in zip(pixel, expected):
                    self.assertAlmostEqual(actual, wanted, delta=tolerance)

    def test_layout_ceil_half_terminal_and_bound(self):
        self.assertEqual(ref.layout(13, 7, 6), [(7, 4), (4, 2), (2, 1), (1, 1)])
        self.assertEqual(ref.layout(1, 1, 6), [(1, 1)])
        self.assertEqual(ref.layout(1, 17, 6), [(1, 9), (1, 5), (1, 3), (1, 2), (1, 1)])
        self.assertEqual(len(ref.layout(16384, 16384, 6)), 6)

    def test_black_preservation_in_all_modes(self):
        for mode in ref.DECODE_MODES:
            for threshold in (0, 1, ref.FP16_MAX):
                self.assert_image_constant(ref.bloom(solid(7, 3, (0, 0, 0)),
                    ref.Params(threshold=threshold), mode=mode), (0, 0, 0), 0)

    def test_dc_gain_independent_of_levels_and_scatter(self):
        rgb = (8., 4., 2.)
        for levels in range(1, 7):
            for scatter in (0., .7, 1.):
                params = ref.Params(levels=levels, scatter=scatter)
                expected = ref.prefilter(rgb, params, mode='none')
                self.assert_image_constant(ref.bloom(solid(17, 9, rgb), params,
                    mode='none'), expected)

    def test_area_average_preserves_mean_for_odd_and_thin_images(self):
        for w, h in ((7, 5), (1, 9), (8, 4), (9, 1), (1, 1)):
            source = [[(float(x + 3 * y),) * 3 for x in range(w)] for y in range(h)]
            result = ref.downsample(source)
            self.assertAlmostEqual(sum(p[0] for row in source for p in row) / (w * h),
                sum(p[0] for row in result for p in row) / (len(result) * len(result[0])))

    def test_float32_integer_geometry_matches_independent_area_oracle(self):
        # Faithful scalar float32 operations of the shader, compared against
        # the independent overlap-rectangle oracle (no shared weight formula).
        f32 = lambda v: struct.unpack('<f', struct.pack('<f', v))[0]
        for size in (*range(1, 258), 8462, 15611, 16383, 16384):
            destination = (size + 1) // 2
            inverse = f32(1 / size)
            parity = 2 * destination - size
            rows = []
            column_sums = [0.] * size
            for j in range(destination):
                # Standard quad's pixel-centre UV retains a half-pixel margin
                # before floor even at the maximum admitted dimensions.
                uv = f32((j + .5) / destination)
                self.assertEqual(math.floor(f32(uv * destination)), j)
                weights = [f32(v * inverse) for v in
                           (parity * j, destination, destination - parity * (j + 1))]
                total = f32(f32(weights[0] + weights[1]) + weights[2])
                weights = [f32(v / total) for v in weights]
                actual = {}
                for index, weight in zip((2 * j - 1, 2 * j, 2 * j + 1), weights):
                    key = min(max(index, 0), size - 1)
                    actual[key] = actual.get(key, 0.) + weight
                    column_sums[key] += weight
                rows.append(actual)
                self.assertAlmostEqual(sum(weights), 1., delta=2e-7)
                wanted = {}
                for index, weight in ref._area_weights(j, size, destination):
                    wanted[index] = wanted.get(index, 0.) + weight
                for index in actual.keys() | wanted.keys():
                    # A unit impulse of FP16-max radiance: catches old 32-unit
                    # edge errors while allowing sub-ULP normalized weights.
                    self.assertAlmostEqual(actual.get(index, 0.) * ref.FP16_MAX,
                        wanted.get(index, 0.) * ref.FP16_MAX, delta=.012,
                        msg=f'size={size} j={j} index={index}')
            for total in column_sums:
                self.assertAlmostEqual(total, destination / size, delta=2e-7)
            for j, actual in enumerate(rows):
                reflected = rows[destination - 1 - j]
                for index, weight in actual.items():
                    self.assertAlmostEqual(weight, reflected.get(size - 1 - index, 0.), delta=2e-7)

    def test_four_tap_even_extraction_matches_area(self):
        for w, h in ((2, 2), (8, 6), (82, 2)):
            image = [[(float((x + 3 * y) % 9), float(x % 4), float(y % 3))
                      for x in range(w)] for y in range(h)]
            filtered = [[ref.prefilter(pixel, mode='gamma2.2') for pixel in row] for row in image]
            expected = ref.downsample(filtered)
            for y, row in enumerate(expected):
                for x, pixel in enumerate(row):
                    actual = tuple(sum(filtered[2 * y + dy][2 * x + dx][c] * .25
                                       for dx in (0, 1) for dy in (0, 1)) for c in range(3))
                    for a, b in zip(actual, pixel):
                        self.assertAlmostEqual(a, b, places=12)

    def test_odd_impulse_symmetry_nonnegative_and_chroma(self):
        image = solid(17, 13, (0., 0., 0.))
        image[6][8] = (8., 4., 2.)
        out = ref.bloom(image, ref.Params(threshold=0), mode='none')
        for y in range(13):
            for x in range(17):
                p = out[y][x]
                self.assertGreaterEqual(min(p), 0)
                self.assertAlmostEqual(p[0], 2 * p[1])
                self.assertAlmostEqual(p[0], 4 * p[2])
                for c in range(3):
                    self.assertAlmostEqual(p[c], out[y][16 - x][c], places=12)
                    self.assertAlmostEqual(p[c], out[12 - y][x][c], places=12)

    def test_scatter_zero_uses_only_first_level(self):
        image = [[(float((x + y) % 4),) * 3 for x in range(11)] for y in range(7)]
        a = ref.bloom(image, ref.Params(levels=1, scatter=0, threshold=0), mode='none')
        b = ref.bloom(image, ref.Params(levels=6, scatter=0, threshold=0), mode='none')
        self.assertEqual(a, b)

    def test_four_fetch_matches_independent_nine_tap_at_arbitrary_phase(self):
        rng = random.Random(93013)
        for w, h in ((1, 1), (1, 7), (9, 1), (7, 5), (8, 6)):
            image = [[tuple(rng.uniform(0, 100) for _ in range(3))
                      for _ in range(w)] for _ in range(h)]
            coordinates = [(rng.uniform(-.5, 1.5), rng.uniform(-.5, 1.5)) for _ in range(100)]
            coordinates += [(x / w, y / h) for x in (0, .5, w - .5, w)
                            for y in (0, .5, h - .5, h)]
            for u, v in coordinates:
                samples = [(ref.bilinear(image, u + dx / w, v + dy / h), wx * wy)
                           for dx, wx in ((-1, .25), (0, .5), (1, .25))
                           for dy, wy in ((-1, .25), (0, .5), (1, .25))]
                expected = tuple(sum(p[c] * weight for p, weight in samples) for c in range(3))
                actual = ref.tent_four_fetch(image, u, v)
                for a, b in zip(actual, expected):
                    self.assertAlmostEqual(a, b, delta=2e-12)

    def test_threshold_and_soft_knee_analytical_values(self):
        hard = ref.Params(threshold=2, knee=0)
        for v in (0, 1, 2, 3, 10):
            self.assertAlmostEqual(ref.prefilter((v,) * 3, hard, mode='none')[0], max(v - 2, 0))
        soft = ref.Params(threshold=2, knee=.5)
        for v, expected in ((0, 0), (1, 0), (2, .25), (3, 1), (4, 2)):
            self.assertAlmostEqual(ref.prefilter((v,) * 3, soft, mode='none')[0], expected)

    def test_legacy_prefilter_is_exactly_alpha_independent(self):
        params = ref.Params(authored_glow_gain=0, highlight_gain=1)
        for exposure in (1, 2 ** 1.5, 4):
            expected = ref.prefilter((1.7, .8, .2), params, exposure=exposure, mode='none')
            for alpha in (0, .5, 1, -1, math.nan, math.inf, -math.inf):
                self.assertEqual(ref.prefilter((1.7, .8, .2, alpha), params,
                                              exposure=exposure, mode='none'), expected)

    def test_authored_masks_are_complementary_without_double_counting(self):
        rgb = (2., .5, .25)
        params = ref.Params(threshold=1, knee=.5, authored_glow_gain=2,
                            highlight_gain=.25)
        exposed = ref.exposed(rgb, mode='none')
        legacy = ref.prefilter(rgb, dataclasses.replace(params, authored_glow_gain=0),
                               mode='none')
        for alpha in (0., .5, 1.):
            actual = ref.prefilter(rgb + (alpha,), params, mode='none')
            expected = tuple(alpha * 2 * value + (1 - alpha) * .25 * highlight
                             for value, highlight in zip(exposed, legacy))
            self.assertEqual(actual, expected)
        self.assertEqual(ref.prefilter(rgb + (0.,), params, mode='none'),
                         tuple(.25 * value for value in legacy))
        self.assertEqual(ref.prefilter(rgb + (1.,), params, mode='none'),
                         tuple(2 * value for value in exposed))

    def test_subthreshold_authored_color_and_alpha_sanitizing(self):
        rgb = (.2, .1, .05)
        params = ref.Params(authored_glow_gain=2, highlight_gain=.05)
        self.assertEqual(ref.prefilter(rgb + (0.,), params, mode='none'), (0., 0., 0.))
        self.assertEqual(ref.prefilter(rgb + (1.,), params, mode='none'), (.4, .2, .1))
        self.assertEqual(ref.prefilter(rgb + (.5,), params, mode='none'), (.2, .1, .05))
        for alpha in (math.nan, -math.inf, -2.):
            self.assertEqual(ref.prefilter(rgb + (alpha,), params, mode='none'), (0., 0., 0.))
        for alpha in (math.inf, 2.):
            self.assertEqual(ref.prefilter(rgb + (alpha,), params, mode='none'), (.4, .2, .1))
        self.assert_image_constant(ref.bloom(solid(5, 3, rgb + (1.,)), params,
                                             mode='none'), (.4, .2, .1))

    def test_authored_extraction_bounds_nonfinite_rgb_before_fp16_store(self):
        params = ref.Params(threshold=0, authored_glow_gain=4, highlight_gain=1)
        self.assertEqual(ref.prefilter((math.inf,) * 3 + (1.,), params, mode='none'),
                         (ref.FP16_MAX,) * 3)
        self.assertEqual(ref.prefilter((math.nan, math.inf, -math.inf, 1.), params,
                                      mode='none'), (0., ref.FP16_MAX, 0.))
        # Missing alpha is the same zero authored mask as the shader's ordered clamp.
        self.assertEqual(ref.prefilter((1., 1., 1.), params, mode='none'), (1., 1., 1.))

    def test_authored_off_composite_matches_exposed_source_at_selected_evs(self):
        image = [[(1.2, .4, .1, 0.), (.3, .6, .9, .5), (.1, .2, .3, 1.)]]
        params = ref.Params(strength=0, authored_glow_gain=2, highlight_gain=.05)
        for ev in (0., 1.5, 2.):
            exposure = 2 ** ev
            result = ref.composite(image, params, exposure=exposure, mode='gamma2.2')
            for actual, source in zip(result[0], image[0]):
                expected = tuple(value * exposure for value in ref.decode(source[:3],
                                                                           'gamma2.2')) + source[3:]
                self.assertEqual(actual, expected)

    def test_exposure_units_and_clamp_order(self):
        p = ref.Params(threshold=2, knee=.5)
        self.assertEqual(ref.prefilter((1, 1, 1), p, exposure=2, mode='none'),
                         ref.prefilter((2, 2, 2), p, exposure=1, mode='none'))
        self.assertEqual(ref.exposed((4, 2, 1), exposure=4, clamp_max=2, mode='none'), (8, 8, 4))
        self.assertEqual(ref.exposed((4, 2, 1), exposure=.5, clamp_max=2, mode='gamma2.2'), (1, 1, .5))

    def test_decode_and_threshold_happen_before_filter(self):
        image = [[(0.,) * 3, (2.,) * 3]]
        result = ref.bloom(image, ref.Params(levels=1, threshold=1, knee=0), mode='gamma2.2')
        expected = (2 ** 2.2 - 1) / 2
        self.assert_image_constant(result, (expected,) * 3)
        self.assertGreater(expected, 1)  # decode(average(codes)) would threshold to zero

    def test_compose_alpha_strength_and_exposure_once(self):
        p = ref.Params(threshold=0, strength=.25)
        result = ref.composite(solid(3, 1, (2., 1., .5, .37)), p, exposure=2, mode='none')
        self.assert_image_constant(result, (5., 2.5, 1.25, .37))
        zero = dataclasses.replace(p, strength=0)
        self.assert_image_constant(ref.composite(solid(1, 1, (2., 1., .5, -.3)), zero,
                                                exposure=2, mode='none'), (4., 2., 1., -.3))
        # Base AgX float32 exposure may exceed FP16 and identity decode may
        # preserve negatives. Neither changes merely by enabling bloom.
        self.assert_image_constant(ref.composite(solid(1, 1, (65504., 2., -1., .2)), zero,
                     exposure=2, mode='none'), (131008., 4., -2., .2))

    def test_maximum_finite_intermediates_and_sanitizing(self):
        for mode in ref.DECODE_MODES:
            result = ref.bloom(solid(5, 3, (ref.FP16_MAX,) * 3),
                ref.Params(threshold=0), exposure=ref.FP16_MAX, mode=mode)
            self.assert_image_constant(result, (ref.FP16_MAX,) * 3, 1e-9)
            composite = ref.composite(solid(1, 1, (ref.FP16_MAX,) * 3),
                ref.Params(strength=1, threshold=0), exposure=ref.FP16_MAX, mode=mode)
            self.assert_image_constant(composite, (ref.FP16_MAX ** 2 + ref.FP16_MAX,) * 3)
        self.assertEqual(ref.exposed((math.nan, math.inf, -math.inf), mode='none'), (0, ref.FP16_MAX, 0))

    def test_invalid_inputs(self):
        for field, values in {'levels': (0, 7, 1.5, True), 'strength': (-1, 1.1, math.inf),
                              'threshold': (-1, 65505, math.nan), 'knee': (-.1, 1.1),
                              'scatter': (-.1, 1.1),
                              'authored_glow_gain': (-.1, 4.1, math.nan, math.inf),
                              'highlight_gain': (-.1, 1.1, math.nan, math.inf)}.items():
            for value in values:
                with self.assertRaises(ValueError):
                    dataclasses.replace(ref.Params(), **{field: value}).validate()
        for size in ((0, 2), (2, 0), (16385, 1), (1.5, 1)):
            with self.assertRaises(ValueError):
                ref.layout(*size)
        for exposure in (0, -1, math.nan, math.inf, 65505):
            with self.assertRaises(ValueError):
                ref.exposed((1, 2, 3), exposure=exposure)
        for kwargs in ({'clamp_max': math.nan}, {'mode': 'bad'}):
            with self.assertRaises(ValueError):
                ref.exposed((1, 2, 3), **kwargs)
        for image in ([], [[]], [[(1, 2)]], [[(1, 2, 3)], []]):
            with self.assertRaises(ValueError):
                ref.bloom(image)

    def test_header_compiles_and_matches_oracle_abi(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('host C++ compiler unavailable')
        source = r'''
#include "src/temporal/bloom.h"
#include <cassert>
#include <limits>
int main() {
 using namespace x3::temporal;
 BloomLayout l{}; assert(prepare_bloom_layout(l,{13,7},6));
 assert(l.count==4 && l.level[0].width==7 && l.level[0].height==4 && l.pixels==39);
 assert(!prepare_bloom_layout(l,{0,1},1) && l.count==4);
 assert(prepare_bloom_layout(l,{1,1},6) && l.count==1 && l.pixels==1);
 BloomParams p{}; BloomConstants c{};
 assert(p.levels==5 && p.strength==.05f && p.threshold==1 && p.knee==.5f && p.scatter==.7f);
 assert(p.authored_glow_gain==0 && p.highlight_gain==.05f);
 const AgxDecode modes[]={AgxDecode::gamma22,AgxDecode::srgb,AgxDecode::none};
 for (AgxDecode mode : modes) {
   BloomParams legacy_params{}; BloomConstants legacy{}, authored{};
   assert(prepare_bloom(legacy,{13,7},{7,4},legacy_params,2,4,mode));
   assert(legacy.radiance[3]==0 && legacy.decode[3]==0);
   legacy_params.authored_glow_gain=2; legacy_params.highlight_gain=.125f;
   assert(prepare_bloom(authored,{13,7},{7,4},legacy_params,2,4,mode));
   assert(authored.radiance[3]==2 && authored.decode[3]==.125f);
   for(unsigned i=0;i<3;++i) assert(authored.decode[i]==legacy.decode[i]);
 }
 assert(prepare_bloom(c,{13,7},{7,4},p,2,4,AgxDecode::srgb));
 assert(c.source[0]==13 && c.destination[1]==4 && c.radiance[0]==2 && c.radiance[1]==4);
 assert(c.radiance[2]==65504 && c.radiance[3]==0);
 assert(c.decode[0]==1 && c.decode[1]==1 && c.decode[2]==0 && c.decode[3]==0);
 const float legacy_decode[3]={c.decode[0],c.decode[1],c.decode[2]};
 p.authored_glow_gain=2; p.highlight_gain=.125f;
 assert(prepare_bloom(c,{13,7},{7,4},p,2,4,AgxDecode::srgb));
 assert(c.radiance[3]==2 && c.decode[3]==.125f);
 for(unsigned i=0;i<3;++i) assert(c.decode[i]==legacy_decode[i]);
 assert(!prepare_bloom(c,{13,7},{7,4},p,0,4,AgxDecode::none) && c.radiance[0]==2);
 assert(!prepare_bloom(c,{13,7},{7,4},p,1,4,static_cast<AgxDecode>(999)));
 assert(!prepare_bloom(c,{16385,7},{7,4},p,1,4,AgxDecode::none));
 p.scatter=std::numeric_limits<float>::quiet_NaN(); assert(!valid_bloom_params(p));
 p={}; p.levels=7; assert(!valid_bloom_params(p));
 p={}; p.knee=1.1f; assert(!valid_bloom_params(p));
 p={}; p.strength=-1; assert(!valid_bloom_params(p));
 p={}; p.threshold=65505; assert(!valid_bloom_params(p));
 p={}; p.authored_glow_gain=4; assert(valid_bloom_params(p));
 p.authored_glow_gain=4.01f; assert(!valid_bloom_params(p));
 p.authored_glow_gain=-1; assert(!valid_bloom_params(p));
 p={}; p.authored_glow_gain=std::numeric_limits<float>::quiet_NaN(); assert(!valid_bloom_params(p));
 p={}; p.authored_glow_gain=std::numeric_limits<float>::infinity(); assert(!valid_bloom_params(p));
 p={}; p.highlight_gain=0; assert(valid_bloom_params(p));
 p.highlight_gain=1; assert(valid_bloom_params(p));
 p.highlight_gain=1.01f; assert(!valid_bloom_params(p));
 p.highlight_gain=-.01f; assert(!valid_bloom_params(p));
 p={}; p.highlight_gain=std::numeric_limits<float>::quiet_NaN(); assert(!valid_bloom_params(p));
 p={}; p.highlight_gain=std::numeric_limits<float>::infinity(); assert(!valid_bloom_params(p));
 assert(kBloomFirstRegister==24 && kBloomRegisterCount==5 && kBloomMaxLevels==6);
 assert(bloom_even_extraction({82,2}) && !bloom_even_extraction({82,1}));
 assert(!bloom_even_extraction({0,2}) && !bloom_even_extraction({2,3}));
 BloomExtractShader shader=BloomExtractShader::gamma22;
 for (unsigned i=0;i<3;++i) {
   assert(select_bloom_extract(shader,{7,1},modes[i]) && static_cast<unsigned>(shader)==i);
   assert(select_bloom_extract(shader,{82,2},modes[i]) && static_cast<unsigned>(shader)==i+3);
 }
 assert(!select_bloom_extract(shader,{0,1},AgxDecode::none) && shader==BloomExtractShader::even_none);
 assert(!select_bloom_extract(shader,{2,2},static_cast<AgxDecode>(999)));
}
'''
        with tempfile.TemporaryDirectory(prefix='x3-bloom-host-') as directory:
            path = Path(directory)
            (path / 'test.cpp').write_text(source)
            subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT),
                            str(path / 'test.cpp'), '-o', str(path / 'test')], check=True, capture_output=True)
            subprocess.run([str(path / 'test')], check=True, capture_output=True)

    def test_authored_extraction_variants_have_exact_compiletime_modes(self):
        for even in (False, True):
            for mode, name in enumerate(('gamma', 'srgb', 'none')):
                shader = ROOT / f'src/temporal/bloom_extract_{"even_" if even else ""}{name}_ps.hlsl'
                text = shader.read_text()
                self.assertIn(f'#define BLOOM_DECODE_MODE {mode}\n', text)
                self.assertEqual('#define BLOOM_EVEN\n' in text, even)
                self.assertIn('#define BLOOM_EXTRACT\n', text)
                self.assertIn('#include "bloom_down_ps.hlsl"', text)


if __name__ == '__main__':
    unittest.main()
