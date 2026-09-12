"""AgX host reference (design §3): curve properties, constants, and header/shader consistency."""
import hashlib
import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import agx_reference as ref  # noqa: E402

TOOL = ROOT / 'tools/analysis/agx_reference.py'
HEADER = ROOT / 'src/temporal/agx.h'
SHADER = ROOT / 'src/temporal/agx.hlsl'
RAMP_JSON = ROOT / 'verification/results/agx-ramp.json'


def ramp(lo_exp=-14, hi_exp=6, count=4096):
    return [2.0 ** (lo_exp + (hi_exp - lo_exp) * i / (count - 1)) for i in range(count)]


class CurveTests(unittest.TestCase):
    def test_neutral_axis_monotone_and_bounded(self):
        prev = -1.0
        for x in ramp():
            out = ref.agx((x, x, x))
            self.assertTrue(all(0.0 <= c <= 1.0 for c in out))
            self.assertGreaterEqual(out[0], prev - 1e-12)
            prev = out[0]

    def test_neutral_axis_stays_neutral(self):
        # The matrices are row-stochastic to 1.4e-4: under a quarter of an 8-bit code.
        for x in ramp(count=512):
            out = ref.agx((x, x, x))
            self.assertLess(max(out) - min(out), 1.0 / 1020, x)
        for name in ref.LOOKS:
            if name == 'golden':
                continue  # golden is a deliberate warm cast
            out = ref.agx((0.18, 0.18, 0.18), look=name)
            self.assertLess(max(out) - min(out), 1.0 / 1020, name)

    def test_mid_grey_and_white(self):
        self.assertAlmostEqual(ref.agx((0.18,) * 3)[0], 0.4967, delta=0.003)
        self.assertAlmostEqual(ref.agx((1.0,) * 3)[0], 0.7867, delta=0.003)
        self.assertGreater(ref.agx((2.0 ** ref.MAX_EV,) * 3)[0], 0.995)
        self.assertLess(ref.agx((2.0 ** ref.MIN_EV,) * 3)[0], 0.002)

    def test_input_clamps(self):
        floor = ref.agx((2.0 ** ref.MIN_EV,) * 3)
        for v in ((0.0,) * 3, (-5.0,) * 3, (1e-30,) * 3):
            self.assertEqual(ref.agx(v), floor, v)
        top = ref.agx((2.0 ** (ref.MAX_EV + 1),) * 3)  # +1 EV absorbs the 4e-5 row-sum slack of M_in
        for v in ((1e6,) * 3, (65504.0,) * 3):
            self.assertEqual(ref.agx(v), top, v)

    def test_contrast_polynomial(self):
        self.assertAlmostEqual(ref.contrast(0.0), -0.00232)
        self.assertAlmostEqual(ref.contrast(1.0), 0.99858, places=9)
        ys = [ref.contrast(i / 4096) for i in range(4097)]
        self.assertTrue(all(b >= a for a, b in zip(ys, ys[1:])))

    def test_log_encoding_round_trip(self):
        for x in ramp(-12, 4, 256):
            self.assertAlmostEqual(ref.log_decode(ref.log_encode(x)), x, delta=x * 1e-12)
        self.assertEqual(ref.log_encode(0.0), 0.0)
        self.assertEqual(ref.log_encode(2.0 ** ref.MIN_EV / 4), 0.0)
        self.assertEqual(ref.log_encode(2.0 ** ref.MAX_EV * 4), 1.0)
        self.assertAlmostEqual(ref.log_decode(-1.0), 2.0 ** ref.MIN_EV)
        self.assertAlmostEqual(ref.log_decode(2.0), 2.0 ** ref.MAX_EV)

    def test_exposure_ev_is_a_scale(self):
        for x in (0.01, 0.18, 1.0, 7.0):
            self.assertEqual(ref.agx((x, x / 2, x / 4), exposure_ev=1.0), ref.agx((2 * x, x, x / 2)))
            self.assertEqual(ref.agx((x,) * 3, exposure_ev=-2.0), ref.agx((x / 4,) * 3))

    def test_looks(self):
        grey = (0.18,) * 3
        golden = ref.agx(grey, look='golden')
        self.assertGreater(golden[0], golden[2])  # warm cast
        none_red = ref.agx((0.5, 0.1, 0.1))
        punchy_red = ref.agx((0.5, 0.1, 0.1), look='punchy')
        self.assertGreater(punchy_red[0] - punchy_red[1], none_red[0] - none_red[1])
        self.assertEqual(ref.agx(grey, look='base'), ref.agx(grey, look='none'))
        self.assertEqual(ref.agx(grey, look_name='punchy'), ref.agx(grey, look='punchy'))
        with self.assertRaises(ValueError):
            ref.agx(grey, look='vivid')
        with self.assertRaises(TypeError):
            ref.agx(grey, looks='none')
        self.assertEqual(ref.LOOKS['golden'], ((1.0, 0.9, 0.5), (0.0, 0.0, 0.0), (0.8, 0.8, 0.8), 0.8))
        self.assertEqual(ref.LOOKS['punchy'], ((1.0, 1.0, 1.0), (0.0, 0.0, 0.0), (1.35, 1.35, 1.35), 1.4))

    def test_decode_modes(self):
        self.assertAlmostEqual(ref.decode((0.5,), 'gamma2.2')[0], 0.5 ** 2.2)
        self.assertAlmostEqual(ref.decode((0.5,), 'srgb')[0], 0.214041, places=5)
        self.assertAlmostEqual(ref.decode((0.03,), 'srgb')[0], 0.03 / 12.92)
        self.assertEqual(ref.decode((0.5, 3.0), 'none'), (0.5, 3.0))
        self.assertGreater(ref.decode((4.0,), 'gamma2.2')[0], 4.0)  # extended above 1, monotone
        self.assertEqual(ref.decode((-1.0,), 'gamma2.2'), (0.0,))
        with self.assertRaises(ValueError):
            ref.decode((1.0,), 'linear')

    def test_tonemap_engine_matches_agx_and_clamps(self):
        engine = (0.5, 0.25, 0.125)
        self.assertEqual(ref.tonemap_engine(engine, exposure=2.0, decode_mode='none'),
                         ref.agx(engine, exposure_ev=1.0))
        self.assertEqual(ref.tonemap_engine(engine), ref.agx(ref.decode(engine)))
        hot = (100.0, 100.0, 100.0)
        self.assertEqual(ref.tonemap_engine(hot, decode_mode='none', clamp_max=2.0), ref.agx((2.0,) * 3))
        self.assertEqual(ref.tonemap_engine(hot, decode_mode='none'), ref.agx(hot))


class ConstantTests(unittest.TestCase):
    def test_matrices_are_mutual_inverses_and_row_stochastic(self):
        for i in range(3):
            self.assertAlmostEqual(sum(ref.M_IN[i]), 1.0, delta=2e-4)
            self.assertAlmostEqual(sum(ref.M_OUT[i]), 1.0, delta=2e-4)
            for j in range(3):
                prod = sum(ref.M_IN[i][k] * ref.M_OUT[k][j] for k in range(3))
                self.assertAlmostEqual(prod, 1.0 if i == j else 0.0, delta=1e-6)

    def test_header_carries_the_reference_constants(self):
        text = HEADER.read_text()
        for row in ref.M_IN + ref.M_OUT:
            for c in row:
                self.assertIn(f'{c!r}f', text, c)
        for c in ref.CONTRAST_COEFFICIENTS + ref.LUMA_WEIGHTS + (ref.MIN_EV, ref.MAX_EV):
            self.assertIn(f'{c!r}f', text, c)
        self.assertIn('kAgxFirstRegister = 8', text)
        self.assertIn('kAgxRegisterCount = 14', text)
        self.assertIn(f'{ref.DECODE_GAMMA!r}f', text)
        for look, (slope, _offset, power, sat) in ref.LOOKS.items():
            if look == 'none':
                continue
            self.assertIn(f'{sat!r}f', text, look)
            self.assertIn(f'{power[0]!r}f', text, look)

    def test_shader_registers_match_header_and_avoid_resolve(self):
        text = SHADER.read_text()
        regs = sorted(int(m) for m in re.findall(r'register\(c(\d+)\)', text))
        self.assertEqual(regs, list(range(8, 22)))
        self.assertEqual(re.findall(r'register\(s(\d+)\)', text), ['0'])
        self.assertIn('static const float3 lumaWeights = float3(0.2126, 0.7152, 0.0722);', text)
        self.assertIn(f'static const float logFloor = {ref.LOG_FLOOR:g};', text)
        self.assertNotIn('pow(v, 2.2)', text)  # output stays display encoded
        code = re.sub(r'//[^\n]*', '', text)
        self.assertNotRegex(code, r'\b(for|while)\b')


class CompiledProgramTests(unittest.TestCase):
    """Stage 2: the embedded ps_3_0 programs are pinned to their sources by the generator's manifests."""
    def test_manifests_pin_the_sources(self):
        for name, source in (('hdr-tonemap-program', 'src/temporal/agx.hlsl'),
                             ('hdr-meter-level0-program', 'src/temporal/hdr_meter_level0_ps.hlsl'),
                             ('hdr-meter-reduce-program', 'src/temporal/hdr_meter_reduce_ps.hlsl')):
            manifest = json.loads((ROOT / 'verification/results' / f'{name}.json').read_text())
            self.assertEqual(manifest['source'], source)
            self.assertEqual(manifest['source_sha256'], hashlib.sha256((ROOT / source).read_bytes()).hexdigest(), name)
            self.assertEqual(manifest['target'], 'ps_3_0')
            header = ROOT / 'src/renderer' / (name.replace('-', '_') + '_inc.h')
            self.assertEqual(manifest['header_sha256'], hashlib.sha256(header.read_bytes()).hexdigest(), name)
            self.assertEqual(manifest['word_count'], header.read_text().count('0x'))

    def test_meter_shader_mirrors_the_reference(self):
        text = (ROOT / 'src/temporal/hdr_meter_level0_ps.hlsl').read_text()
        self.assertIn('static const float3 lumaWeights = float3(0.2126, 0.7152, 0.0722);', text)
        self.assertEqual(sorted(int(m) for m in re.findall(r'register\(c(\d+)\)', text)), [0, 1, 2, 3])
        self.assertIn('log2(clamp(L, meter.x, meter.y))', text)
        self.assertIn('acc * (1.0 / 16.0)', text)


class RampTests(unittest.TestCase):
    def test_ramp_table_is_deterministic(self):
        a = json.dumps(ref.ramp_table(), sort_keys=True)
        b = json.dumps(ref.ramp_table(), sort_keys=True)
        self.assertEqual(a, b)
        table = ref.ramp_table()
        self.assertEqual(table['rows'][0]['input'], 0.001)
        self.assertEqual(table['rows'][-1]['input'], 64.0)
        self.assertLess(len(json.dumps(table)), 32768)

    def test_cli_writes_the_committed_ramp(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'ramp.json'
            run = subprocess.run([sys.executable, str(TOOL), '--out', str(out)], capture_output=True, text=True, check=True)
            self.assertIn('mid-grey 0.18 -> 0.4967', run.stdout)
            self.assertEqual(json.loads(out.read_text()), ref.ramp_table())
        self.assertTrue(RAMP_JSON.exists(), 'run tools/analysis/agx_reference.py to regenerate')
        self.assertEqual(json.loads(RAMP_JSON.read_text()), ref.ramp_table())


if __name__ == '__main__':
    unittest.main()
