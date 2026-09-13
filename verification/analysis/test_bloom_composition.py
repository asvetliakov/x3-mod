"""Independent composition oracle and bounded FP16 display-stage experiment.

Run as unittest for controls, or directly to print measured quantization stats.
The oracle uses existing independently authored double AgX stages, and an
independent scalar RCAS expression. It does not reproduce the shader's bloom
sampling: the input is already reconstructed exposed-linear bloom. Sampling,
float32 compiler evaluation, GPU output conversion and timing are separate
qualifications. The reported errors isolate display-stage storage only.
"""
import itertools
import json
import math
from pathlib import Path
import random
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import agx_reference as agx


def half(value, rounding='nearest'):
    """IEEE binary16 nearest-even or toward-zero store, returned as double."""
    encoded = struct.pack('<e', value)
    result = struct.unpack('<e', encoded)[0]
    if rounding == 'towardzero':
        if abs(result) > abs(value):
            bits = struct.unpack('<H', encoded)[0]
            result = struct.unpack('<e', struct.pack('<H', bits - 1))[0]
    elif rounding != 'nearest':
        raise ValueError(rounding)
    return result


def exposed_scene(engine, exposure, mode, clamp_max):
    limit = clamp_max if clamp_max > 0 else agx.FP16_MAX
    return tuple(min(c, limit) * exposure for c in agx.decode(engine[:3], mode))


def composition(engine, bloom, strength, exposure=1, mode='gamma2.2',
                clamp_max=0, look='none'):
    base = exposed_scene(engine, exposure, mode, clamp_max)
    added = tuple(c + strength * b for c, b in zip(base, bloom))
    # The oracle reaches the independently specified inset with the full sum.
    display = agx.output(agx.look(agx.agx_core(added), look))
    return (*display, engine[3])


def rcas_cross(taps, gain):
    """Equivalent RCAS equations, expressed as scalar channel/ring reductions."""
    taps = [tuple(min(1, max(0, c)) for c in t[:3]) for t in taps]
    center = taps[2]
    ring = [taps[i] for i in (0, 1, 3, 4)]
    lumas = [sum(a * b for a, b in zip(t, (0.5, 1, 0.5))) for t in taps]
    deviation = abs(sum(lumas[i] for i in (0, 1, 3, 4)) / 4 - lumas[2])
    noise = 1 - 0.5 * min(1, deviation / max(max(lumas) - min(lumas), 1 / 256))
    lo = [min(t[c] for t in ring) for c in range(3)]
    hi = [max(t[c] for t in ring) for c in range(3)]
    limits = [max(-a / max(4 * b, 1 / 4096),
                  (1 - b) / min(4 * a - 4, -1 / 4096)) for a, b in zip(lo, hi)]
    weight = max(-0.1875, min(max(limits), 0)) * gain * noise
    output = [(sum(t[c] for t in ring) * weight + center[c]) / (1 + 4 * weight)
              for c in range(3)]
    return tuple(min(max(v, min(lo[c], center[c])), max(hi[c], center[c]))
                 for c, v in enumerate(output))


def code8(value):
    # Nearest-even model isolates this experiment; no claim of GPU tie policy.
    return round(min(1, max(0, value)) * 255)


def experiment():
    """Deterministic high-contrast, near-black, chroma and smooth patch corpus.

    Every engine and bloom input is representable FP16. All three decoders and
    looks, clamp off/on, EV -3/0/+2, strength 0/.05/1 and sharpness 0/.75/1
    participate. Sharpness zero takes the actual direct, unstaged path.
    """
    rng = random.Random(0xB100A6)
    patches = []
    for kind in ('highcontrast', 'nearblack', 'chroma', 'smooth'):
        for _ in range(20):
            base = 2 ** rng.uniform(-10, 2)
            scene, bloom = [], []
            for tap in range(5):
                if kind == 'highcontrast':
                    rgb = [rng.choice((0, 2**-14, 0.18, 1, 8, 65504)) for _ in range(3)]
                elif kind == 'nearblack':
                    rgb = [2 ** rng.uniform(-24, -7) for _ in range(3)]
                elif kind == 'chroma':
                    rgb = [rng.choice((0, 1)) * 2 ** rng.uniform(-7, 5) for _ in range(3)]
                else:
                    rgb = [max(0, base * (1 + rng.uniform(-0.01, 0.01))) for _ in range(3)]
                scene.append(tuple(half(c) for c in (*rgb, rng.random())))
                # Mix dark, bright and smooth bloom; sampler qualification owns
                # which spatial input can occur, this test owns composition.
                bloom.append(tuple(half(rng.choice((0, 0.0001, 0.02, 0.5, 16, 65504)))
                                   for _ in range(3)))
            patches.append((kind, scene, bloom))
    stats = {r: dict(channels=0, max_absolute=0, sum_absolute=0,
                    max_code_difference=0, changed_codes=0, worst=None)
             for r in ('nearest', 'towardzero')}
    combinations = itertools.product(agx.DECODE_MODES, agx.LOOKS, (0, 0.25),
                                     (0.125, 1, 4), (0, 0.05, 1))
    cases = 0
    for mode, look, clamp_max, exposure, strength in combinations:
        for kind, engine, bloom in patches:
            fused = [composition(e, b, strength, exposure, mode, clamp_max, look)
                     for e, b in zip(engine, bloom)]
            for sharpness in (0, 0.75, 1):
                gain = 2 ** (-2 * (1 - sharpness))
                expected = rcas_cross(fused, gain) if sharpness else fused[2][:3]
                cases += 1
                for rounding, record in stats.items():
                    stage = [tuple(half(c, rounding) for c in t) for t in fused]
                    actual = rcas_cross(stage, gain) if sharpness else fused[2][:3]
                    for a, b in zip(actual, expected):
                        error = abs(a - b)
                        codes = abs(code8(a) - code8(b))
                        record['channels'] += 1
                        record['sum_absolute'] += error
                        record['changed_codes'] += int(codes != 0)
                        record['max_code_difference'] = max(record['max_code_difference'], codes)
                        if error > record['max_absolute']:
                            record.update(max_absolute=error, worst=dict(kind=kind, mode=mode,
                                look=look, clamp_max=clamp_max, exposure=exposure,
                                strength=strength, sharpness=sharpness))
    for record in stats.values():
        record['mean_absolute'] = record.pop('sum_absolute') / record['channels']
    return dict(cases=cases, patches=len(patches), storage_only=True,
                gpu_verified=False, arbitrary_tolerance_used=False, rounding=stats)


class BloomCompositionTests(unittest.TestCase):
    def test_zero_strength_preserves_existing_transform_and_alpha(self):
        for mode, look, scale in itertools.product(agx.DECODE_MODES, agx.LOOKS, (0.125, 1, 65504)):
            for engine in ((-4, 0, 65504, 0.375), (0.18, 1, 8, 1), (0, 0, 0, 0)):
                output = composition(engine, (65504,) * 3, 0, scale, mode, look=look)
                self.assertEqual(output[:3], agx.tonemap_engine(engine[:3], scale, mode, look))
                self.assertEqual(output[3], engine[3])

    def test_exposure_and_clamp_precede_addition(self):
        engine, bloom = (4, 1, 0.25, 0.5), (2, 0.1, 0.4)
        self.assertEqual(exposed_scene(engine, 8, 'none', 0.5), (4, 4, 2))
        self.assertEqual(composition(engine, bloom, 0.5, 8, 'none', 0.5)[:3],
                         agx.agx((5, 4.05, 2.2)))
        self.assertNotEqual(composition(engine, bloom, 0.5, 8, 'none', 0.5)[:3],
                            agx.agx((0.5, 0.5, 0.45)))

    def test_no_fp16_store_before_inset_and_identity_negatives_remain(self):
        base = exposed_scene((-6158, 0.01, 65504, 1), 4, 'none', 0)
        self.assertEqual(base, (-24632, 0.04, 262016))
        # Large opposite channels are consequential after the non-diagonal
        # inset; clipping before it changes even non-white output channels.
        rgb = composition((-6158, 0.01, 65504, 1), (0, 1, 0), 1, 4, 'none')[:3]
        clipped = agx.agx(tuple(min(65504, max(-65504, c)) for c in
                               (base[0], base[1] + 1, base[2])))
        self.assertNotEqual(rgb, clipped)

    def test_half_rounding_modes_and_bounds(self):
        rng = random.Random(812)
        for x in [0, 1, 2**-25, 0.5003, -0.5003] + [rng.uniform(-1, 1) for _ in range(5000)]:
            near, trunc = half(x), half(x, 'towardzero')
            self.assertLessEqual(abs(near - x), 2**-12)
            self.assertLessEqual(abs(trunc - x), 2**-11)
            self.assertLessEqual(abs(trunc), abs(x))
        self.assertNotEqual(half(0.5003), half(0.5003, 'towardzero'))

    def test_rcas_constant_and_cross_bounds(self):
        rng = random.Random(891)
        for gain in (0.25, 2**-0.5, 1):
            for color in ((0, 0, 0), (1, 1, 1), (0.5, 0.1, 0.9)):
                for a, b in zip(rcas_cross([color] * 5, gain), color):
                    self.assertAlmostEqual(a, b)
            for _ in range(100):
                taps = [tuple(rng.random() for _ in range(3)) for _ in range(5)]
                for c, value in enumerate(rcas_cross(taps, gain)):
                    self.assertLessEqual(value, max(t[c] for t in taps))
                    self.assertGreaterEqual(value, min(t[c] for t in taps))

    def test_shader_insertion_contract(self):
        text = (ROOT / 'src/temporal/bloom_agx_ps.hlsl').read_text()
        body = '\n'.join(line.split('//')[0] for line in text[text.index('float4 main'):].splitlines())
        expected = ('decodeEngine(scene.rgb)', 'min(v, exposure.y)', 'v *= exposure.x',
                    'bloomComposite(v, bloomTent(reconstructedBloom, uv))',
                    'agxTonemapExposed(v, scene.a)')
        indices = [body.index(item) for item in expected]
        self.assertEqual(indices, sorted(indices))
        self.assertNotIn('bloomExposed(', body)
        self.assertNotIn('_pp', body)

    def test_quantization_is_measured_not_claimed_exact(self):
        report = experiment()
        self.assertEqual(report['cases'], 38880)
        for record in report['rounding'].values():
            self.assertGreater(record['max_absolute'], 0)
            self.assertGreater(record['changed_codes'], 0)
            self.assertTrue(math.isfinite(record['mean_absolute']))
            # No arbitrary acceptance threshold: storage error combines with
            # shader and sampler errors only in the future GPU differential.


if __name__ == '__main__':
    print(json.dumps(experiment(), indent=2, sort_keys=True))
