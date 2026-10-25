"""The long distance fade (docs/architecture/fog-visible-distance.md): fog_distance 1 bit identity, the fade law and the
geometric tail bins of the host reference against the shader's maths and the production constants, the distance programs
and their native-validator record, the horizon level of the density cache (priority, shared budget, SETA travel,
fail-closed paths) and the setting's schema, launcher and DLL parsing. Native clang++; no Wine, no D3D device."""
import hashlib
import importlib.util
import io
import contextlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import fog_density_shader_slots as slots  # noqa: E402

spec = importlib.util.spec_from_file_location('fog_density_shader_reference', ROOT / 'tools/analysis/fog_density_shader_reference.py')
ref = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ref)
CHROMA = (0.20072728, 1.0, 0.120704934)
LOOK = ('fog_density_march_look', 'fog_density_repair_look', 'fog_density_march_look_q4', 'fog_density_repair_look_q4')
DISTANCE = ('fog_density_march_distance', 'fog_density_repair_distance', 'fog_density_march_distance_q4', 'fog_density_repair_distance_q4')
# Today's four look programs as committed before the long fade (d8a4aa18): fog_distance 1 draws exactly these.
PINNED = {'fog_density_march_look': '6a347ac2c07d4be702c8b267f704b1aad2cdde16cd729565a2f01a0acf83ed25',
          'fog_density_repair_look': '155a82e2833141db22e2f0688eb11c4aef2a614f612127a348fc739a49b85056'}
VALUES = ('nan', '0.5', '1', '1.25', '1.5', '1.7333', '1.7334', '2', '3', '3.5')
PROGRAM = r'''
#include "fog_look_math.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    using namespace x3m::renderer;
    const FogLookTuning t{};
    for (int i = 1; i < argc; ++i) {
        const float d = std::strtof(argv[i], nullptr);
        const bool horizon = fog_distance_horizon(t, d);
        float rows[fog_look_rows][4], law[4];
        const float chroma[3] = {0.20072728f, 1.0f, 0.120704934f}, radiance[3] = {1.f, 1.f, 1.f};
        fog_look_constants(t, chroma, radiance, 0, rows);
        const float cap = fog_distance_cap(t, d, horizon);
        fog_distance_constants(t, cap, .25f, rows, law);
        std::printf("%s %d %d %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n", argv[i], int(fog_distance_programs(d)),
                    int(horizon), double(cap), double(fog_distance_cap(t, d, false)), double(rows[0][3]),
                    double(rows[10][1]), double(rows[10][2]), double(law[0]), double(law[1]), double(law[2]),
                    double(law[3]));
    }
    std::printf("REGISTERS %u %u\n", fog_distance_register, fog_distance_rows);
    return 0;
}
'''


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def record(name):
    return json.loads((ROOT / ('verification/results/%s-program.json' % name.replace('_', '-'))).read_text())


class Uniform:
    """The same stored density everywhere; counts the taps per level."""
    def __init__(self, value=.8):
        self.value, self.taps = value, {'fine': 0, 'far': 0, 'horizon': 0}

    def sample(self, points, distance, camera, counts=None):
        return np.full(len(points), self.value, np.float32)

    def sample_level(self, name, points, camera):
        self.taps[name] += len(points)
        return np.full(len(points), self.value, np.float32)


def fade(s, cap, start=65000.):
    e = np.clip((s - .75 * cap) / (.25 * cap), 0, 1)
    return np.minimum(1., start ** 2 / s ** 2) * (1 - e * e * (3 - 2 * e))


class FogDistance(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('clang++') or shutil.which('g++')
        cls.temporary = tempfile.TemporaryDirectory(prefix='x3-fog-distance-')
        source = Path(cls.temporary.name) / 'distance.cpp'
        source.write_text(PROGRAM)
        tool = Path(cls.temporary.name) / 'distance'
        subprocess.run([compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT / 'src/renderer'), str(source), '-o', str(tool)], check=True)
        cls.rows = {}
        for line in subprocess.run([str(tool), *VALUES], check=True, capture_output=True, text=True).stdout.splitlines():
            parts = line.split()
            cls.rows[parts[0]] = parts[1:]
        cls.host = Path(cls.temporary.name) / 'horizon-host'
        sources = [ROOT / 'verification/probe/fog_density_horizon_host.cpp', ROOT / 'src/fog/fog_density_cache.cpp', ROOT / 'src/fog/fog_density_generator.cpp']
        subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-pthread', '-DX3M_FOG_DENSITY_TEST_HOOKS']
                       + [str(s) for s in sources] + ['-o', str(cls.host)], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_host(self, mode):
        done = subprocess.run([str(self.host), mode], capture_output=True, timeout=600)
        text = done.stdout.decode()
        self.assertEqual((done.returncode, re.findall(r'^CHECK (\S+) FAIL', text, re.M)), (0, []), text[-3000:])
        self.assertRegex(text, r'RESULT PASS checks=\d+ failures=0')
        return text

    # ------------------------------------------------------------------ fog_distance 1: bit identity
    def test_distance_one_keeps_todays_programs_constants_and_levels(self):
        # The constants: 1 (and anything not above 1) selects no distance program and no horizon level.
        for value in ('nan', '0.5', '1'):
            self.assertEqual(self.rows[value][:2], ['0', '0'], value)
        # Today's four look programs keep their bytes (pinned before the change; the q4 pair against the march/repair pins
        # by construction: FOG_MARCH_SCALE only), and their sources never see FOG_DISTANCE.
        for name, value in PINNED.items():
            self.assertEqual(record(name)['bytecode_sha256'], value, name)
        for name in LOOK:
            words = slots.words_of(slots.PROGRAMS[name])
            self.assertEqual(record(name)['bytecode_sha256'], hashlib.sha256(struct.pack('<%dI' % len(words), *words)).hexdigest(), name)
            self.assertNotIn('FOG_DISTANCE', (ROOT / record(name)['source']).read_text(), name)
        field = (ROOT / 'src/fog/fog_density_field_inc.h').read_text()
        self.assertEqual(field.count('#ifdef FOG_DISTANCE'), 4)  # declarations, far step, fade, tail loop
        self.assertNotIn('#ifndef FOG_DISTANCE', field)
        # The renderer: the distance set only from fog_distance_programs, the horizon only from fog_distance_horizon; the
        # legacy path keeps its 36 rows and 8 samplers, the cache its two levels.
        source = (ROOT / 'src/renderer/fog_pass.cpp').read_text()
        self.assertIn('const bool distance_wanted = fog_distance_programs(density_config_.distance) && !distance_unbuildable_;', source)
        self.assertIn('const bool horizon_wanted = density_distance_set_ && fog_distance_horizon(density_config_.look, density_config_.distance);', source)
        self.assertIn('else if (horizon_wanted && !density_->horizon() && !density_->horizon_refused())\n        density_->enable_horizon();', source)
        prefill = source[source.index('bool FogPass::prefill_density('):source.index('void FogPass::release_density_worker(')]
        self.assertNotIn('horizon', prefill)  # a prefill never allocates the horizon level
        self.assertIn('const bool distance = density && density_distance_set_;', source)
        self.assertIn('distance ? density_distance_rows : density ? fog_constant_rows : 22u', source)
        self.assertIn('const UINT samplers = distance ? 9u : density ? 8u : 7u;', source)
        self.assertIn('if (density_->horizon() && !density_staging_[h]) {', source)
        self.assertIn('if (density_->horizon() && !density_atlas_[h]) {', source)
        cache = (ROOT / 'src/fog/fog_density_cache.h').read_text()
        self.assertIn('bool horizon_on_ = false, horizon_posted_ = false;', cache)
        # The two-level cache witnesses run unchanged on the new cache (test_fog_density_cache, test_fog_handover).

    # ------------------------------------------------------------------ constants and law
    def test_constants_mirror_the_production_header(self):
        for value in VALUES:
            programs, horizon, cap, cap_far, c25w, c35y, c35z, s0, start, ready, zero = self.rows[value]
            d = float(value)
            self.assertEqual(int(programs), int(np.float32(d) > 1), value)
            self.assertEqual(int(horizon), int(ref.distance_horizon(d)), value)
            self.assertEqual(np.float32(cap), ref.distance_cap(d, bool(int(horizon))), value)
            self.assertEqual(np.float32(cap_far), ref.distance_cap(d, False), value)
            rows, _ = ref.look_constants(CHROMA)
            law = ref.distance_rows(rows, ref.distance_cap(d, bool(int(horizon))), .25)
            np.testing.assert_array_equal(np.array([c25w, c35y, c35z], np.float32), rows[[0, 10, 10], [3, 1, 2]], value)
            np.testing.assert_array_equal(np.array([s0, start, ready, zero], np.float32), law, value)
        self.assertEqual(self.rows['3'][2:4], ['337500', '195000'])
        self.assertEqual(self.rows['1.7333'][1], '0'); self.assertEqual(self.rows['1.7334'][1], '1')
        self.assertEqual(self.rows['3.5'][2], '337500')  # clamped to 3
        self.assertEqual(self.rows['REGISTERS'], ['36', '2'])

    def test_fade_law(self):
        cap = 337500.
        s = np.array([10000., 65000., 112500., 225000., 337500.])
        w = fade(s, cap)
        np.testing.assert_allclose(w[:2], 1.)
        self.assertAlmostEqual(w[2], (65000 / 112500) ** 2, places=12)  # .334 at today's cap: continuous at the tail start
        self.assertAlmostEqual(w[3], .0834, places=3)
        self.assertEqual(w[4], 0.)
        # Appearance band (design table): w >= .05 from 54.4 km, w >= .5 from 18.4 km; equivalent path 23.1 km.
        grid = np.arange(0., cap, 5.)
        ww = fade(np.maximum(grid, 1.), cap)
        self.assertAlmostEqual(grid[ww >= .05].max() / 5000, 54.4, delta=.1)
        self.assertAlmostEqual(grid[ww >= .5].max() / 5000, 18.4, delta=.1)
        self.assertAlmostEqual(np.trapezoid(ww, grid) / 5000, 23.1, delta=.05)

    def test_tail_bins_in_shader_order(self):
        for L, first, last in ((337500., 3133., 9143.), (200000., 1630., 2856.)):  # the design rows: x3 and 40 km
            centres, widths = ref.tail_bins(L)
            ratio = (L / 112500.) ** (1 / 40)
            self.assertAlmostEqual(centres[0, 0] / np.sqrt(ratio), 112500., delta=.05)  # bin 0 starts at today's cap
            self.assertAlmostEqual(float(widths.sum(dtype=np.float64)), L - 112500., delta=1e-4 * L)
            np.testing.assert_allclose(widths[0, 1:] / widths[0, :-1], ratio, rtol=2e-6)
            self.assertAlmostEqual(float(widths[0, 0]), first, delta=1.)
            self.assertAlmostEqual(float(widths[0, -1]), last, delta=1.)
        # Shorter geometry rays get their own ratio; a ray inside today's cap has no tail.
        centres, widths = ref.tail_bins(np.array([150000.]))
        self.assertAlmostEqual(float(widths.sum(dtype=np.float64)), 37500., delta=10.)

    def test_march_against_the_analytic_law(self):
        # A uniform medium without coverage variation: T of the 24 + 40 + 40 bins against exp(-sigma rho' int w ds).
        tuning = dict(ref.TUNING, coverage_variation=0.)
        rho = ((.8 - .35) / .65) ** 2
        for distance in (None, 2., 3.):
            store = Uniform()
            _, T = ref.look_march(np.zeros(3), np.array([[1., 0., 0.]]), np.array([1e9]), False, CHROMA, store, 3.75e-6,
                                  tuning=tuning, distance=distance)
            sigma = 3.75e-6 * ref.look_constants(CHROMA, tuning=tuning)[1]
            if distance is None:
                s = np.linspace(0., 112500., 200001)
                t = np.clip((s - 65000.) / 47500., 0, 1); w = 1 - t * t * (3 - 2 * t)
            else:
                cap = float(ref.distance_cap(distance, True)); s = np.linspace(1e-3, cap, 400001); w = fade(s, cap)
            self.assertLess(abs(T[0] - np.exp(-sigma * rho * np.trapezoid(w, s))), 1e-4, distance)
            self.assertEqual(store.taps['horizon'] > 0, distance == 3. or distance == 2.)
        # Equivalent paths of the design table: 17.75 km today, 23.1 km at x3.

    def test_horizon_readiness_and_absence(self):
        d = np.array([[1., 0., 0.], [0., 1., 0.]])
        # Readiness 0 (or no horizon level): the horizon atlas is never read and nothing past 195,000 contributes.
        for kwargs in (dict(horizon_ready=0.), dict(horizon=False)):
            store = Uniform()
            _, T0 = ref.look_march(np.zeros(3), d, np.full(2, 1e9), False, CHROMA, store, 3.75e-6, distance=3., **kwargs)
            self.assertEqual(store.taps['horizon'], 0, kwargs)

        class Raises(Uniform):
            def sample_level(self, name, points, camera):
                if name == 'horizon':
                    raise AssertionError('horizon read at readiness 0')
                return super().sample_level(name, points, camera)
        ref.look_march(np.zeros(3), d, np.full(2, 1e9), False, CHROMA, Raises(), 3.75e-6, distance=3., horizon_ready=0.)
        # Readiness ramps the tail in monotonically: more readiness, more extinction.
        Ts = [ref.look_march(np.zeros(3), d, np.full(2, 1e9), False, CHROMA, Uniform(), 3.75e-6, distance=3., horizon_ready=r)[1][0]
              for r in (0., .5, 1.)]
        self.assertTrue(Ts[0] > Ts[1] > Ts[2])
        # Without the horizon level the cap is 39 km.
        self.assertEqual(ref.distance_cap(3., False), np.float32(195000.))

    def test_shader_source_carries_the_reference_constants(self):
        text = (ROOT / 'src/fog/fog_density_field_inc.h').read_text()
        for snippet in ('float4 horizon_local : register(c36);', 'float4 distance_law : register(c37);',
                        'sampler2D horizon_atlas : register(s8);',
                        'max(min(distance,distance_law.y)-12000.0,0.0)/float(FOG_FAR_BINS)',
                        'min(1.0,distance_law.x/(bin.y*bin.y))', 'float q = exp2(log2(distance/distance_law.y)*0.025);',
                        'float2 tail = float2(distance_law.y*sqrt(q),(q-1.0)/sqrt(q));', '[loop] for (int j=0; j<40; ++j) {',
                        'float h = saturate((tail.x-175000.0)/20000.0);', 'float2 w = float2(1.0-h,h*distance_law.z);',
                        'rho = look_density(rho/presence,cover)*presence*(distance_law.x/(tail.x*tail.x))*(1.0-e*e*(3.0-2.0*e));',
                        'tail.x *= q;'):
            self.assertIn(snippet, text)
        self.assertEqual((ref.DISTANCE_BLEND, ref.TAIL_BINS, ref.DISTANCE_FAR_CAP), ((175000., 20000.), 40, 195000.))

    # ------------------------------------------------------------------ programs
    def test_distance_programs_recorded_counted_and_validated(self):
        text = (ROOT / 'tools/shaders/generate_rigid_motion_pixel.py').read_text()
        pass_source = (ROOT / 'src/renderer/fog_pass.cpp').read_text()
        validated = json.loads((ROOT / 'verification/results/fog-visible-distance-2026-10-25/validate_programs.json').read_text())['programs']
        for name in DISTANCE:
            r = record(name)
            header = ROOT / ('src/renderer/%s_program_inc.h' % name)
            self.assertIn("'%s': dict(" % name, text)
            self.assertIn('#include "%s_program_inc.h"' % name, pass_source)
            self.assertEqual(r['source_sha256'], digest(ROOT / r['source']), name)
            self.assertEqual(r['header_sha256'], digest(header), name)
            for path, value in r['includes'].items():
                self.assertEqual(value, digest(ROOT / path), (name, path))
            self.assertIn('#define FOG_DISTANCE\n', (ROOT / r['source']).read_text())
            words = slots.words_of(header)
            self.assertEqual(r['bytecode_sha256'], hashlib.sha256(struct.pack('<%dI' % len(words), *words)).hexdigest(), name)
            count = slots.count(words)
            v = validated[name]
            self.assertEqual((v['slots'], v['words'], v['texture_instructions']), (count['slots'], count['words'], count['texture_instructions']), name)
            self.assertEqual((v['end'], v['messages'], v['max_constant_reads']), ('00000000', [], 1), name)
            self.assertEqual(count['loops'], 2, name)  # the 64-bin loop and the tail loop stay rolled
            # Low thousands at most (AGENTS.md, shader slot budget); about 300 slots over the look program.
            look = slots.count(slots.words_of(slots.PROGRAMS[name.replace('_distance', '_look')]))
            self.assertLess(count['slots'], 1000, name)
            self.assertEqual(count['texture_instructions'] - look['texture_instructions'], 8, name)  # 2 levels x (rho + tap) x 2

    # ------------------------------------------------------------------ the horizon level of the cache
    def test_horizon_cache_priority_budget_and_content(self):
        text = self.run_host('logic')
        fill = dict(re.findall(r'(\w+)=(-?\d+)', re.search(r'^FILL (.*)$', text, re.M).group(1)))
        self.assertEqual(fill['fine_resident_3'], fill['fine_resident_2'])  # a cold start: fine first
        self.assertEqual(fill['far_resident_3'], fill['far_resident_2'])
        self.assertGreater(int(fill['first_horizon_frame']), int(fill['fine_resident_2']) - 8)
        self.assertLessEqual(int(fill['max_horizon_slab']), 2 * 128 * 128)
        self.assertNotRegex(text, r'differing_bytes=[1-9]')

    def test_horizon_under_seta_travel(self):
        text = self.run_host('seta')
        rows = {(m.group(1), m.group(2)): dict(re.findall(r'(\w+)=([-0-9.]+)', m.group(3)))
                for m in re.finditer(r'^(SETA|SETA_STRESS) levels=(\d) (.*)$', text, re.M)}
        two, three = rows[('SETA', '2')], rows[('SETA', '3')]
        self.assertEqual((two['far_zero'], three['far_zero'], three['horizon_zero']), ('0', '0', '0'))
        self.assertGreaterEqual(int(three['fine_resident']), int(two['fine_resident']) - 45)
        share = float(re.search(r'horizon_share=([0-9.]+)', text).group(1))
        self.assertLessEqual(share, .10)

    def test_horizon_fail_closed(self):
        text = self.run_host('alloc')
        for name in ('horizon_allocation_failure_keeps_two_levels', 'refused_horizon_fills_two_levels', 'threaded_worker_fills_three_levels'):
            self.assertIn('CHECK %s PASS' % name, text)
        logic = self.run_host('logic')
        for name in ('drop_is_immediate', 'drop_is_final', 'drop_frees_the_horizon_cache_at_the_next_post', 'horizon_needs_a_started_cache',
                     'dropped_horizon_is_not_filled_or_uploaded', 'base_levels_settle_after_drop', 'reset_drops_horizon'):
            self.assertIn('CHECK %s PASS' % name, logic)
        # The renderer side: every horizon failure that is not a lost device is a fallback, never a refusal of the fog.
        source = (ROOT / 'src/renderer/fog_pass.cpp').read_text()
        for reason in ('staging', 'atlas', 'lock', 'upload'):
            self.assertIn('horizon_fallback("%s")' % reason, source)
        body = source[source.index('void FogPass::horizon_fallback('):]
        body = body[:body.index('\n}\n')]
        self.assertIn('density_->drop_horizon(reason);', body)
        self.assertNotIn('density_refused_', body); self.assertNotIn('refuse(', body)
        fog = (ROOT / 'src/proxy/motion_output_fog_inc.h').read_text()
        self.assertIn('event=horizon_fallback reason=%s', fog)
        self.assertIn('static constexpr const char* ready_events[3] = {"far_ready", "fine_ready", "horizon_ready"};', fog)

    # ------------------------------------------------------------------ setting
    def launch(self, *args):
        spec = importlib.util.spec_from_file_location('fog_manage', ROOT / 'tools/manage.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.WINE = Path(sys.executable)  # no CrossOver on this host: --dry-run only checks that the loader exists
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / 'X3AP.exe').touch()
            argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', directory, *args]
            stdout, stderr = io.StringIO(), io.StringIO()
            with patch('sys.argv', argv), contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                try:
                    module.main()
                    status = 0
                except SystemExit as error:
                    status = error.code
            return status, stdout.getvalue(), stderr.getvalue()

    def test_schema_template_launcher_and_dll(self):
        sys.path.insert(0, str(ROOT / 'tools/config'))
        import schema  # noqa: E402
        entry = next(e for e in schema.SETTINGS if e['key'] == 'fog_distance')
        self.assertEqual((entry['env'], entry['type'], entry['default'], entry['launcher'], entry['requires']),
                         ('X3M_FOG_DISTANCE', 'float', '3', '--fog-distance', ('volumetric_fog_range',)))
        self.assertIn('\n;fog_distance = 3\n', (ROOT / 'assets/x3m.ini').read_text())
        self.assertIn('{"X3M_FOG_DISTANCE", "fog_distance", Type::Float, "3"', (ROOT / 'src/config/config_schema_inc.h').read_text())
        base = ('--motion-output', '--taa', '--hdr', '--object-trace', '--object-lifetime', '--ownership', '--shadow-replay-depth',
                '--shadow-cascades', 'default', '--volumetric-fog')
        stored = ('--volumetric-fog-range', 'stored')
        status, output, error = self.launch(*base, *stored)
        self.assertEqual(status, 0, error); self.assertIn('"X3M_FOG_DISTANCE": "3"', output)
        status, output, error = self.launch(*base, *stored, '--fog-distance', '1')
        self.assertEqual(status, 0, error); self.assertIn('"X3M_FOG_DISTANCE": "1.0"', output)
        status, output, error = self.launch(*base, *stored, '--fog-distance', '2.25')
        self.assertEqual(status, 0, error); self.assertIn('"X3M_FOG_DISTANCE": "2.25"', output)
        for bad in ('0.9', '3.01', 'nan'):
            status, _, error = self.launch(*base, *stored, '--fog-distance', bad)
            self.assertEqual(status, 2, bad); self.assertIn('--fog-distance must be within [1, 3]', error)
        # Refused without the stored range: under --vanilla nothing fills the range in, so a bare flag is refused.
        status, _, error = self.launch('--fog-distance', '2')
        self.assertEqual(status, 2); self.assertIn('--fog-distance requires --volumetric-fog-range stored', error)
        status, _, error = self.launch(*base, '--fog-distance', '2')
        self.assertEqual(status, 2); self.assertIn('--fog-distance requires --volumetric-fog-range stored', error)
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('const DWORD distance_length = x3m::config::get(L"X3M_FOG_DISTANCE", setting, 32);', capture)
        self.assertIn('v >= renderer::fog_distance_min && v <= renderer::fog_distance_max', capture)
        self.assertIn('log("volumetric_fog_distance_mode distance=%g requested=%s refused=%s programs=%s horizon=%u cap_km=%.1f",', capture)
        self.assertIn('hooked.motion_output.configure_volumetric_fog_distance(volumetric_fog_distance);', capture)


if __name__ == '__main__':
    unittest.main()
