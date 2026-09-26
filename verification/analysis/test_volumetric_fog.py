"""Volumetric sun fog, host side (docs/architecture/volumetric-fog.md, "Stage 1 implementation").

Compiles src/renderer/fog_pass_math.h and verification/probe/fog_reference.h
(neither needs d3d9.h) with the native compiler: the strength ladder, the sun
radiance law, the capability gate order, the sector latch, the ps_3_0 slot
counts of the four embedded programs, and the reference's invariants (no
occluder: F = 1; an occluder: a shaft; the composite's energy bound). Then the
fixture runner's parser/acceptance on a synthetic report, the launcher's CLI
contract and the production wiring. No Wine, no device.
"""
from pathlib import Path
import contextlib
import importlib.util
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from verification.analysis.test_capture_bloom_lifetime import extract_function

ROOT = Path(__file__).resolve().parents[2]
PROGRAMS = ('march', 'composite', 'sky_level0', 'sky_reduce')
DRIVER = r'''
#include "fog_pass_math.h"
#include "ps3_program_slots.h"
#include "fog_reference.h"
#include <cstdio>
using namespace x3m::renderer;
int main() {
    std::printf("steps=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", fog_strength_next(0.f), fog_strength_next(.005f), fog_strength_next(.012f), fog_strength_next(.02f), fog_strength_next(.03f), fog_strength_next(.05f));
    float e[3]; const std::int32_t white[3] = {256, 256, 256}, warm[3] = {255, 128, 0}, none[3] = {0, -5, 0}, hot[3] = {4000, 0, 0};
    std::printf("white=%d %.5f\n", fog_sun_radiance(white, e), e[0]);
    std::printf("warm=%d %.5f %.5f %.5f\n", fog_sun_radiance(warm, e), e[0], e[1], e[2]);
    std::printf("none=%d %.5f\n", fog_sun_radiance(none, e), e[0] + e[1] + e[2]);
    std::printf("hot=%d %.4f\n", fog_sun_radiance(hot, e), e[0]);
    FogCapabilityInputs ok{0xffff0300u, 0xfffe0300u, 512, 224, 4, 16, 0, 0, 0, true, true, true};
    auto reason = [](FogCapabilityInputs in) { const char* r = fog_capability(in); return r ? r : "ok"; };
    std::printf("gate_all=%s\n", reason(ok));
    auto v = ok; v.pixel_shader_version = 0xffff0200u; std::printf("gate_ps=%s\n", reason(v));
    v = ok; v.ps30_instruction_slots = 200; std::printf("gate_slots=%s\n", reason(v));
    v = ok; v.fp16_target_blending = -1; std::printf("gate_fp16=%s\n", reason(v));
    v = ok; v.blend_invsrcalpha = false; std::printf("gate_blend=%s\n", reason(v));
    v = ok; v.stretch_rect = false; std::printf("gate_stretch=%s\n", reason(v));
    // Sector latch: nothing without cards; ramps up over fog_card_ramp frames; holds; ramps down after the hold.
    FogSectorLatch latch;
    std::printf("latch_idle=%.3f\n", latch.update(10));
    latch.card(11); float w = 0; for (std::uint64_t f = 11; f < 11 + 45; ++f) w = latch.update(f);
    std::printf("latch_half=%.3f\n", w);
    for (std::uint64_t f = 56; f < 200; ++f) w = latch.update(f);
    std::printf("latch_full=%.3f\n", w);
    w = latch.update(11 + fog_card_hold); std::printf("latch_hold=%.3f\n", w);
    for (std::uint64_t f = 12 + fog_card_hold; f < 12 + fog_card_hold + 200; ++f) w = latch.update(f);
    std::printf("latch_out=%.3f recent=%d\n", w, latch.cards_recent(12 + fog_card_hold + 200));
    // Failure policy: a lost device never counts or disables; a failed allocation disables; the third other failure disables.
    std::printf("policy=%u%u%u%u%u\n", unsigned(fog_failure_action(true, true, 2)), unsigned(fog_failure_action(true, false, 2)), unsigned(fog_failure_action(false, true, 0)), unsigned(fog_failure_action(false, false, 1)), unsigned(fog_failure_action(false, false, 2)));
    // Cut: ends the hold unless a card was bound in the cut frame itself; the weight ramps, never jumps.
    FogSectorLatch gate; gate.card(100); for (std::uint64_t f = 100; f < 300; ++f) gate.update(f);
    gate.cut(300); const float after_cut = gate.update(300); for (std::uint64_t f = 301; f < 400; ++f) w = gate.update(f);
    FogSectorLatch view; view.card(100); for (std::uint64_t f = 100; f < 300; ++f) view.update(f);
    view.card(300); view.cut(300); for (std::uint64_t f = 300; f < 400; ++f) view.update(f);
    std::printf("latch_cut=%.3f,%.3f,%d view=%.3f\n", after_cut, w, gate.cards_recent(400), view.weight());
    FogSectorLatch jump; jump.card(5); jump.update(5); std::printf("latch_jump=%.3f\n", jump.update(5000)); // a long gap moves at most one ramp, toward off here
#define SLOTS(name, file) { const std::uint32_t words[] = {
#define SLOTS_END(name) }; std::printf("slots_" name "=%u words=%zu\n", ps3_program_slots(words, sizeof words / sizeof words[0]), sizeof words / sizeof words[0]); }
    SLOTS("march", 0)
#include "fog_march_program_inc.h"
    SLOTS_END("march")
    SLOTS("composite", 0)
#include "fog_composite_program_inc.h"
    SLOTS_END("composite")
    SLOTS("sky_level0", 0)
#include "fog_sky_level0_program_inc.h"
    SLOTS_END("sky_level0")
    SLOTS("sky_reduce", 0)
#include "fog_sky_reduce_program_inc.h"
    SLOTS_END("sky_reduce")
    // Reference invariants on a 64x48 frame: a wall at z = 5000 everywhere.
    const unsigned W = 64, H = 48; fog_reference::Params p; p.m11 = 1.7320508; p.m00 = p.m11 * H / W; p.tau_max = .05; p.radius = 3000;
    std::vector<float> depth(std::size_t(W) * H * 4, 0.f);
    for (std::size_t i = 0; i < depth.size(); i += 4) { depth[i] = float(p.m22 + p.m32 / 5000.); depth[i + 2] = 5000.f; }
    auto open = fog_reference::march(depth, W, H, p, [](const double*) { return 1.; });
    double lo = 2, hi = -1; for (double f : open) { lo = std::min(lo, f); hi = std::max(hi, f); }
    std::printf("open=%.4f,%.4f\n", lo, hi);
    auto slab = fog_reference::march(depth, W, H, p, [](const double* q) { return q[2] > 500 && q[2] < 1500 ? 0. : 1.; }); // shadowed between z 500 and 1500
    double mean = 0; for (double f : slab) mean += f; mean /= double(slab.size());
    // Expected on the axis: 1 - (exp(-500 / R) - exp(-1500 / R)) / (1 - exp(-5000 / R)) to first order in tau (exact as tau -> 0).
    std::printf("slab=%.4f expected=%.4f\n", mean, 1 - (std::exp(-500. / 3000) - std::exp(-1500. / 3000)) / (1 - std::exp(-5000. / 3000)));
    const double engine[3] = {.5, .25, 1.5}, sky[3] = {.001, .006, .029};
    const auto px = fog_reference::composite(depth, open, W, H, 20, 20, engine, sky, p);
    double worst = 0; for (unsigned c = 0; c < 3; ++c) worst = std::max(worst, px.linear_out[c] - std::pow(engine[c], 2.2) * px.transmittance);
    std::printf("composite_t=%.5f gain=%.5f bound=%.5f\n", px.transmittance, worst, 4 * 3.14159 * fog_phase(p.g, 1.) * (1 - std::exp(-p.tau_max)));
    const double mild[3] = {.01, .02, .03}; const auto hue = fog_reference::hue_of(mild); std::printf("hue_luma=%.5f\n", .2126 * hue[0] + .7152 * hue[1] + .0722 * hue[2]);
    std::printf("hue_clamped=%.3f\n", fog_reference::hue_of(sky)[2]); // a saturated blue sky: 0.029 / 0.0066 = 4.4, held at the mock's 4
    const double black[3] = {0, 0, 0}; std::printf("hue_black=%.1f\n", fog_reference::hue_of(black)[0]);
    return 0;
}
'''


class FogMathTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('clang++') or shutil.which('c++')
        assert compiler, 'A host C++ compiler is required'
        with tempfile.TemporaryDirectory(prefix='x3-fog-math-') as temporary:
            source, executable = Path(temporary) / 'driver.cpp', Path(temporary) / 'driver'
            source.write_text(DRIVER)
            build = subprocess.run([compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/renderer'), '-I', str(ROOT / 'verification/probe'),
                                    str(source), '-o', str(executable)], capture_output=True, text=True)
            assert build.returncode == 0, build.stderr
            run = subprocess.run([str(executable)], capture_output=True, text=True, check=True)
        cls.values = dict(line.split('=', 1) for line in run.stdout.splitlines())

    def test_strength_ladder_and_sun_radiance(self):
        v = self.values
        self.assertEqual(v['steps'], '0.005,0.010,0.020,0.030,0.050,0.005')  # 0.012 moves up to 0.02; the top wraps
        self.assertEqual(v['white'], '1 3.14159')
        ok, r, g, b = v['warm'].split()
        self.assertEqual(ok, '1')
        self.assertAlmostEqual(float(r), 3.14159 * (255 / 256) ** 2.2, places=4)
        self.assertAlmostEqual(float(g), 3.14159 * 0.5 ** 2.2, places=4)
        self.assertEqual(float(b), 0.0)
        self.assertEqual(v['none'], '0 0.00000')
        self.assertAlmostEqual(float(v['hot'].split()[1]), 3.14159 * 4 ** 2.2, places=2)  # clamped at Color0 = 4

    def test_capability_gate_order(self):
        v = self.values
        self.assertEqual([v[k] for k in ('gate_all', 'gate_ps', 'gate_slots', 'gate_fp16', 'gate_blend', 'gate_stretch')],
                         ['ok', 'ps_3_0', 'ps_slots', 'fp16_target_blending', 'blend_factors', 'stretch_rect'])

    def test_sector_latch(self):
        v = self.values
        self.assertEqual(v['latch_idle'], '0.000')
        self.assertEqual(v['latch_half'], '0.500')
        self.assertEqual(v['latch_full'], '1.000')
        self.assertEqual(v['latch_hold'], '1.000')  # still inside the hold
        self.assertEqual(v['latch_out'], '0.000 recent=0')
        self.assertEqual(v['latch_jump'], '0.000')
        self.assertEqual(v['latch_cut'], '0.989,0.000,0 view=1.000')
        self.assertEqual(v['policy'], '00212')

    def test_program_slots_and_provenance(self):
        for name in PROGRAMS:
            slots, words = re.fullmatch(r'(\d+) words=(\d+)', self.values['slots_' + name]).groups()
            self.assertTrue(0 < int(slots) <= 512, (name, slots))
            record = (ROOT / ('verification/results/fog-%s-program.json' % name.replace('_', '-'))).read_text()
            self.assertIn('"word_count": %s' % words, record)
            self.assertIn('"source": "src/fog/fog_%s_ps.hlsl"' % name, record)

    def test_reference_invariants(self):
        v = self.values
        self.assertEqual(v['open'], '1.0000,1.0000')
        mean, expected = (float(x) for x in re.fullmatch(r'([\d.]+) expected=([\d.]+)', v['slab']).groups())
        self.assertAlmostEqual(mean, expected, delta=0.02)
        self.assertLess(mean, 0.8)
        t, gain, bound = (float(x) for x in re.fullmatch(r'([\d.]+) gain=([\d.]+) bound=([\d.]+)', v['composite_t']).groups())
        self.assertTrue(0.95 < t < 1 and 0 < gain <= bound, (t, gain, bound))
        self.assertAlmostEqual(float(v['hue_luma']), 1.0, places=4)
        self.assertEqual(v['hue_black'], '1.0')
        self.assertEqual(v['hue_clamped'], '4.000')


class FogRunnerTests(unittest.TestCase):
    def setUp(self):
        sys.path.insert(0, str(ROOT / 'verification/probe'))
        spec = importlib.util.spec_from_file_location('run_fog_pass', ROOT / 'verification/probe/run_fog_pass.py')
        self.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.module)

    def report(self, **overrides):
        lines = ['CAPS ps=ffff0300 vs=fffe0300 ps30_slots=512 rts=4 stretch_from_textures=1', 'ATTACH enabled=1 largest_program_slots=224 references=6',
                 'CHECK attach_enabled PASS']
        for scene, kind in (('phase0', 'map_twin'), ('phase0', 'analytic'), ('phase3', 'map_twin'), ('phase3', 'analytic'), ('cascade0_absent', 'map_twin'), ('r32f', 'against_view_depth_lane')):
            lines.append('REFERENCE scene=%s kind=%s pixels=1000 mean_abs=%s max=0.0625 within_one_step=%d' % (scene, kind, overrides.get('mean_abs', '0.000143'), overrides.get('within', 1000)))
        for phase in (0, 3):
            lines += ['ORACLE scene=phase%d label=shaft mean=0.8595 pixels=6700 PASS' % phase, 'ORACLE scene=phase%d label=sky_clear mean=1.0000 pixels=9000 not_one=0 PASS' % phase,
                      'APPLY scene=phase%d channels=30 over=%d max_rel=0.000975 alpha_changed=0 bound_bad=0 darker=0 gain_bound=0.129 sky_cap_error=0.0017' % (phase, overrides.get('over', 0))]
        lines.append('RESET PASS references=15 allocations=2')
        for w, h, sky in self.module.TIMING_BLOCKS:
            lines.append('TIMING width=%d height=%d sky=%d fenced_on_ms=1.1 fenced_off_ms=0.02 submit_ms=0.19 chain_ms=1.08 gpu_ms=0.89 gpu_timestamp_ms=-1.0 timestamp_samples=0 device_calls=172 samples=15' % (w, h, sky))
            if not sky:
                lines.append('TIMING_QUADS width=%d height=%d march_ms=0.57 sky_level_ms=0.35 sky_reduce_ms=0.31 composite_ms=0.55 sum_ms=1.8 samples=6' % (w, h))
        lines.append('RESULT PASS checks=1 failures=0')
        return self.module.parse('\n'.join(lines))

    def test_parse_and_accept(self):
        report = self.report()
        self.module.accept(report)
        self.assertEqual((report['check_count'], len(report['reference']), len(report['timing']), len(report['timing_quads'])), (1, 6, 6, 3))
        self.assertFalse(report['timing'][0]['budget']['within_cap'])  # 1.08 ms fenced chain against the 1.0 ms cap: reported, not an acceptance term
        self.assertEqual(report['timing'][0]['device_calls'], 172)

    def test_card_report_requires_each_gpu_witness(self):
        labels = ('card_mask_actual_no_writes', 'card_mask_state_and_calls', 'card_mask_lasterror',
                  'card_mask_native_hresult_rgb_alpha', 'card_mask_failed_draw_restore', 'card_mask_auxiliary_unchanged',
                  'card_resources_cold', 'card_resources_warm', 'card_resources_reset', 'card_resources_rewarm', 'card_resources_detach', 'card_teardown_window')
        lines = ['CHECK ' + label + ' PASS' for label in labels]
        self.module.accept(self.module.parse('\n'.join(lines + ['RESULT PASS checks=12 failures=0'])), cards_only=True)
        with self.assertRaises(AssertionError):
            self.module.accept(self.module.parse('\n'.join(lines[:-1] + ['RESULT PASS checks=11 failures=0'])), cards_only=True)
        with self.assertRaises(AssertionError):
            self.module.accept(self.module.parse('\n'.join([line.replace('PASS', 'FAIL') for line in lines] + ['RESULT FAIL checks=12 failures=12'])), cards_only=True)

    def test_accept_refuses_a_bad_reference_and_a_law_violation(self):
        with self.assertRaises(AssertionError):
            self.module.accept(self.report(mean_abs='0.02'))
        with self.assertRaises(AssertionError):
            self.module.accept(self.report(within=900))
        with self.assertRaises(AssertionError):
            self.module.accept(self.report(over=1))


class FogLauncherTests(unittest.TestCase):
    BASE = ('--motion-output', '--taa', '--hdr', '--object-trace', '--object-lifetime', '--ownership', '--shadow-replay-depth', '--shadow-cascades', 'default')

    def launch(self, *args, environment=None):
        spec = importlib.util.spec_from_file_location('fog_manage', ROOT / 'tools/manage.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / 'X3AP.exe').touch()
            argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', directory, *args]
            stdout, stderr = io.StringIO(), io.StringIO()
            with patch('sys.argv', argv), patch.dict(os.environ, environment or {}), contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                try:
                    module.main()
                    status = 0
                except SystemExit as error:
                    status = error.code
            return status, stdout.getvalue(), stderr.getvalue()

    def test_defaults_values_and_inherited_environment(self):
        status, output, error = self.launch(*self.BASE, '--volumetric-fog')
        self.assertEqual(status, 0, error)
        for line in ('"X3M_VOLUMETRIC_FOG": "1"', '"X3M_VOLUMETRIC_FOG_STRENGTH": "0.02"'):
            self.assertIn(line, output)
        # The pass timing is part of --perf since the logging tiers (2026-09-26): the DLL reads X3M_VOLUMETRIC_FOG_TIMING or
        # X3M_PERF; the launcher never sends the variable.
        self.assertNotIn('X3M_VOLUMETRIC_FOG_TIMING', output)
        # The superseded --volumetric-fog-anisotropy was removed (the stored look carries
        # its own two-lobe phase); the DLL keeps its own g = 0.3 default.
        self.assertNotIn('X3M_VOLUMETRIC_FOG_ANISOTROPY', output)
        status, output, error = self.launch(*self.BASE, '--volumetric-fog', '0.05', '--perf')
        self.assertEqual(status, 0, error)
        for line in ('"X3M_VOLUMETRIC_FOG_STRENGTH": "0.05"', '"X3M_PERF": "1"'):
            self.assertIn(line, output)
        # --volumetric-fog-timing (logging tiers) and --volumetric-fog-everywhere (2026-09-26, user decision) are removed.
        for removed in ('--volumetric-fog-timing', '--volumetric-fog-everywhere'):
            status, _, error = self.launch(*self.BASE, '--volumetric-fog', removed)
            self.assertEqual(status, 2)
            self.assertIn('unrecognized arguments', error)
        status, output, error = self.launch(*self.BASE, environment={'X3M_VOLUMETRIC_FOG': '1', 'X3M_VOLUMETRIC_FOG_EVERYWHERE': '1', 'X3M_VOLUMETRIC_FOG_TIMING': '1'})
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_VOLUMETRIC_FOG": "0"', output)
        self.assertNotIn('X3M_VOLUMETRIC_FOG_EVERYWHERE', output)  # removed 2026-09-26; an inherited value is dropped
        self.assertNotIn('X3M_VOLUMETRIC_FOG_TIMING', output)

    def test_card_option(self):
        for mode in ('keep', 'replace'):
            status, output, error = self.launch(*self.BASE, '--volumetric-fog', '--volumetric-fog-cards', mode)
            self.assertEqual(status, 0, error)
            self.assertIn('"X3M_VOLUMETRIC_FOG_CARDS": "' + mode + '"', output)
        status, output, error = self.launch(*self.BASE, '--volumetric-fog', environment={'X3M_VOLUMETRIC_FOG_CARDS': 'replace'})
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_VOLUMETRIC_FOG_CARDS": "keep"', output)
        self.assertEqual(self.launch(*self.BASE, '--volumetric-fog', '--volumetric-fog-cards', 'all')[0], 2)

    def test_range_option(self):
        status, output, error = self.launch(*self.BASE, '--volumetric-fog')
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_VOLUMETRIC_FOG_RANGE": "legacy"', output)
        for mode in ('legacy', 'stored'):
            status, output, error = self.launch(*self.BASE, '--volumetric-fog', '--volumetric-fog-range', mode)
            self.assertEqual(status, 0, error)
            self.assertIn('"X3M_VOLUMETRIC_FOG_RANGE": "' + mode + '"', output)
        # An inherited variable never selects the experimental field, with or without the fog.
        for fog in ((), ('--volumetric-fog',)):
            status, output, error = self.launch(*self.BASE, *fog, environment={'X3M_VOLUMETRIC_FOG_RANGE': 'stored'})
            self.assertEqual(status, 0, error)
            self.assertIn('"X3M_VOLUMETRIC_FOG_RANGE": "legacy"', output)
        self.assertEqual(self.launch(*self.BASE, '--volumetric-fog-range', 'stored')[0], 2)
        self.assertEqual(self.launch(*self.BASE, '--volumetric-fog', '--volumetric-fog-range', 'far')[0], 2)
        self.assertEqual(self.launch(*self.BASE, '--volumetric-fog', '0', '--volumetric-fog-range', 'stored')[0], 2)
        self.assertEqual(self.launch(*self.BASE, '--volumetric-fog', '0', '--volumetric-fog-range', 'legacy')[0], 0)
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('fog_env(L"X3M_VOLUMETRIC_FOG_RANGE")==6 && !wcscmp(setting,L"stored")', capture)
        self.assertIn('volumetric_fog_range_stored=volumetric_fog_requested &&', capture)

    def test_shadow_pass_and_far_bins_options_are_removed(self):
        # --fog-shadow-pass (the visibility grid, fog-shadow-pass.md) and --fog-far-bins (step B's 24 bins, fog-gpu-cost.md)
        # were removed on 2026-09-25 (docs/verification/launcher-options-inventory.md, "4. Removed"): both are
        # unknown arguments, their variables are neither sent nor inherited, and the DLL reads neither.
        stored = ('--volumetric-fog', '--volumetric-fog-range', 'stored')
        for extra in (('--fog-shadow-pass', 'on'), ('--fog-shadow-pass', 'off'), ('--fog-far-bins', '24'), ('--fog-far-bins', '40')):
            status, _, error = self.launch(*self.BASE, *stored, *extra)
            self.assertEqual(status, 2, extra); self.assertIn('unrecognized arguments', error)
        status, output, error = self.launch(*self.BASE, *stored, environment={'X3M_FOG_SHADOW_PASS': '1', 'X3M_FOG_FAR_BINS': '24'})
        self.assertEqual(status, 0, error); self.assertNotIn('X3M_FOG_SHADOW_PASS', output); self.assertNotIn('X3M_FOG_FAR_BINS', output)
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        for gone in ('X3M_FOG_SHADOW_PASS', 'X3M_FOG_FAR_BINS', 'volumetric_fog_shadow_pass', 'volumetric_fog_far_bins'):
            self.assertNotIn(gone, capture, gone)
        fog_pass = (ROOT / 'src/renderer/fog_pass.cpp').read_text(); header = (ROOT / 'src/renderer/fog_pass.h').read_text()
        for gone in ('shadow_pass=', 'far_bins=', 'far_bins_', 'grid_', 'FogGridReport', 'fog_shadow_grid', '_far24_', '_grid_program_inc.h'):
            self.assertNotIn(gone, fog_pass, gone); self.assertNotIn(gone, header, gone)
        self.assertIn('constexpr unsigned fog_far_bins = 40;', (ROOT / 'src/renderer/fog_look_math.h').read_text())
        self.assertFalse((ROOT / 'src/renderer/fog_shadow_grid.h').exists())
        self.assertNotIn('shadow_pass', (ROOT / 'src/proxy/motion_output_fog_inc.h').read_text())

    def test_march_scale_option(self):
        # --fog-march-scale {2,4} -> X3M_FOG_MARCH_SCALE (fog-gpu-cost.md step C), default 4 since Run 77 C2 (the quarter-resolution
        # march), 2 the opt-out; the variable is always written. An explicit 4 needs the stored range; an inherited value never
        # decides it.
        stored = ('--volumetric-fog', '--volumetric-fog-range', 'stored')
        status, output, error = self.launch(*self.BASE, *stored)
        self.assertEqual(status, 0, error); self.assertIn('"X3M_FOG_MARCH_SCALE": "4"', output); self.assertNotIn('fog march scale:', error)
        status, output, error = self.launch(*self.BASE)
        self.assertEqual(status, 0, error); self.assertIn('"X3M_FOG_MARCH_SCALE": "4"', output)
        status, output, error = self.launch(*self.BASE, *stored, '--fog-march-scale', '2')
        self.assertEqual(status, 0, error); self.assertIn('"X3M_FOG_MARCH_SCALE": "2"', output)
        status, output, error = self.launch(*self.BASE, *stored, '--fog-march-scale', '4')
        self.assertEqual(status, 0, error); self.assertIn('"X3M_FOG_MARCH_SCALE": "4"', output)
        status, output, error = self.launch(*self.BASE, '--volumetric-fog', '--fog-march-scale', '2')
        self.assertEqual(status, 0, error); self.assertIn('"X3M_FOG_MARCH_SCALE": "2"', output)
        status, _, error = self.launch(*self.BASE, '--volumetric-fog', '--fog-march-scale', '4')
        self.assertEqual(status, 2); self.assertIn('--fog-march-scale 4 requires --volumetric-fog-range stored', error)
        self.assertEqual(self.launch(*self.BASE, *stored, '--fog-march-scale', '3')[0], 2)
        status, output, error = self.launch(*self.BASE, *stored, environment={'X3M_FOG_MARCH_SCALE': '2'})
        self.assertEqual(status, 0, error); self.assertIn('"X3M_FOG_MARCH_SCALE": "4"', output)
        # The DLL: absent, "4" or an invalid value draw the default 4 under the stored range, exactly "2" the half-resolution
        # march. FogPass defaults to 4, refuses a spacing other than 2/4 at prepare and falls back to 2 itself (programs, target).
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('char march_scale_value[40]="4";', capture)
        self.assertIn('march_scale_half=!wcscmp(setting,L"2");', capture)
        self.assertIn('if(!march_scale_half&&wcscmp(setting,L"4"))march_scale_refusal="invalid";', capture)
        self.assertIn('volumetric_fog_march_scale=march_scale_half?renderer::fog_march_scale_half:renderer::fog_march_scale_quarter;', capture)
        self.assertIn('log("volumetric_fog_march_scale scale=%u requested=%s refused=%s"', capture)
        self.assertIn('hooked.motion_output.configure_volumetric_fog_march_scale(volumetric_fog_march_scale);', capture)
        self.assertIn('needs_px=%u', capture)
        self.assertIn('fog_density_config_.march_scale = scale;', (ROOT / 'src/proxy/motion_output.h').read_text())
        math = (ROOT / 'src/renderer/fog_look_math.h').read_text()
        self.assertIn('constexpr unsigned fog_march_scale_half = 2, fog_march_scale_quarter = 4, fog_march_scale_default = fog_march_scale_quarter;', math)
        self.assertIn('unsigned march_scale=fog_march_scale_default;', (ROOT / 'src/renderer/fog_pass.h').read_text())
        fragment = (ROOT / 'src/proxy/motion_output_fog_inc.h').read_text()
        self.assertIn('log("fog_march_scale_refused device=%llu frame=%llu reason=%s requested=%u drawn=%u"', fragment)
        self.assertIn('ramp_frames=%u march_scale=%u"', fragment)
        fog_pass = (ROOT / 'src/renderer/fog_pass.cpp').read_text()
        self.assertIn('if(!fog_march_scale_valid(config.march_scale))return E_INVALIDARG;', fog_pass)
        self.assertNotIn('fog_march_scale_default', fog_pass)  # every refusal falls back to fog_march_scale_half
        resources = extract_function(fog_pass, 'HRESULT FogPass::density_resources(')
        # The quarter target before the programs that draw into it; it lives only while they draw.
        self.assertLess(resources.index('if(density_config_.march_scale==fog_march_scale_quarter&&!quarter_&&width_&&height_){'), resources.index('auto make_set='))
        self.assertIn('if(density_march_scale_!=fog_march_scale_quarter)release_quarter();', resources)
        self.assertIn('release_quarter();', extract_function(fog_pass, 'void FogPass::release_targets('))

    def test_dust_motes_option(self):
        # --fog-dust-motes N[,SIZE[,STREAK]] -> X3M_FOG_DUST_MOTES=N,SIZE,STREAK (fog-dust-motes.md section 4): omitted under
        # the stored range the Run 70 B/B2 default 1300,3,128 with MAX_PX 8 (2026-09-23), without it off; explicit 0 is the
        # opt-out; always written so an inherited value cannot decide the motes; a count above 0 needs the stored range.
        stored = ('--volumetric-fog', '--volumetric-fog-range', 'stored')
        status, output, error = self.launch(*self.BASE, *stored)
        self.assertEqual(status, 0, error); self.assertIn('"X3M_FOG_DUST_MOTES": "1300,3,128"', output)
        self.assertIn('"X3M_FOG_MOTES_MAX_PX": "8"', output)
        status, output, error = self.launch(*self.BASE, *stored, environment={'X3M_FOG_MOTES_MAX_PX': '16'})
        self.assertEqual(status, 0, error); self.assertIn('"X3M_FOG_MOTES_MAX_PX": "16"', output)  # the user's value stands
        for environment in (None, {'X3M_FOG_MOTES_MAX_PX': '16'}):
            status, output, error = self.launch(*self.BASE, *stored, '--fog-dust-motes', '0', environment=environment)
            self.assertEqual(status, 0, error); self.assertIn('"X3M_FOG_DUST_MOTES": "0,4,128"', output); self.assertNotIn('X3M_FOG_MOTES_', output)
            status, output, error = self.launch(*self.BASE, '--volumetric-fog', environment=environment)  # no stored range: off
            self.assertEqual(status, 0, error); self.assertIn('"X3M_FOG_DUST_MOTES": "0,4,128"', output); self.assertNotIn('X3M_FOG_MOTES_', output)
        status, output, error = self.launch(*self.BASE, *stored, '--fog-dust-motes', '2048,4')
        self.assertEqual(status, 0, error); self.assertIn('"X3M_FOG_DUST_MOTES": "2048,4,128"', output); self.assertNotIn('X3M_FOG_MOTES_MAX_PX', output)
        for value, expected in (('2048', '2048,4,128'), ('64,2', '64,2,128'), ('8192,16,0', '8192,16,0'), ('512,6,512', '512,6,512'), ('0', '0,4,128'), ('8192,2.123456789,511.987654321', '8192,2.12346,511.988')):
            status, output, error = self.launch(*self.BASE, *stored, '--fog-dust-motes', value)
            self.assertEqual(status, 0, (value, error)); self.assertIn('"X3M_FOG_DUST_MOTES": "%s"' % expected, output)
        for value in ('63', '8193', '2048,1', '2048,17', '2048,4,513', '2048,4,-1', 'abc', '2048,,4', '2048,4,128,1', '2048,nan'):
            self.assertEqual(self.launch(*self.BASE, *stored, '--fog-dust-motes', value)[0], 2, value)
        status, _, error = self.launch(*self.BASE, '--volumetric-fog', '--fog-dust-motes', '2048')
        self.assertEqual(status, 2); self.assertIn('--fog-dust-motes requires --volumetric-fog-range stored', error)
        self.assertEqual(self.launch(*self.BASE, '--fog-dust-motes', '2048')[0], 2)
        self.assertEqual(self.launch(*self.BASE, '--volumetric-fog', '--fog-dust-motes', '0')[0], 0)  # explicit off needs no stored range
        status, output, error = self.launch(*self.BASE, *stored, environment={'X3M_FOG_DUST_MOTES': '2048,4,128'})
        self.assertEqual(status, 0, error); self.assertIn('"X3M_FOG_DUST_MOTES": "1300,3,128"', output)
        status, output, error = self.launch(*self.BASE, '--volumetric-fog', environment={'X3M_FOG_DUST_MOTES': '2048,4,128'})
        self.assertEqual(status, 0, error); self.assertIn('"X3M_FOG_DUST_MOTES": "0,4,128"', output)
        # An inherited MAX_PX survives only with the motes on; the other tunables were removed on 2026-09-26 (values baked)
        # and an inherited value is always dropped.
        tunables = {'X3M_FOG_MOTES_MAX_PX': '12', 'X3M_FOG_MOTES_GAIN': '4', 'X3M_FOG_MOTES_SEED': '9'}
        status, output, error = self.launch(*self.BASE, *stored, '--fog-dust-motes', '0', environment=tunables)
        self.assertEqual(status, 0, error); self.assertNotIn('X3M_FOG_MOTES_', output)
        for extra in ((), ('--fog-dust-motes', '2048')):
            status, output, error = self.launch(*self.BASE, *stored, *extra, environment=tunables)
            self.assertEqual(status, 0, error); self.assertIn('"X3M_FOG_MOTES_MAX_PX": "12"', output)
            self.assertNotIn('X3M_FOG_MOTES_GAIN', output); self.assertNotIn('X3M_FOG_MOTES_SEED', output)
        # The DLL: stored range only, the whole triple must parse, tunables only with the option on, one mode line; an
        # overlong value and a request without the stored range each log one line instead of being ignored silently.
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('const DWORD motes_length=x3m::config::get(L"X3M_FOG_DUST_MOTES",setting,32);', capture)
        self.assertIn('volumetric_fog_motes_mode enabled=0 invalid=1 reason=overlong length=%lu', capture)
        self.assertIn('volumetric_fog_motes_mode enabled=0 reason=requires_stored_range count=%lu', capture)
        self.assertIn('renderer::fog_mote_option(unsigned(n),values[0],values[1],motes)&&motes.count', capture)
        self.assertIn('std::swprintf(name,std::size(name),L"X3M_FOG_MOTES_%hs",field.name);', capture)
        self.assertIn('volumetric_fog_motes_mode enabled=1 count=%u', capture)
        self.assertIn('hooked.motion_output.configure_volumetric_fog_dust_motes(volumetric_fog_motes);', capture)
        motes = (ROOT / 'src/renderer/fog_mote_math.h').read_text()
        # MAX_PX is the one tunable left (2026-09-26); the others are the baked struct defaults.
        for name in ('RADIUS', 'NEAR', 'GAIN', 'SOFT', 'DRIFT'):
            self.assertNotIn('{"%s", &FogMoteTuning' % name, motes)
        self.assertNotIn('X3M_FOG_MOTES_SEED', capture)
        self.assertIn('float radius = 1000.f, near_fade = 25.f;', motes)
        self.assertIn('float max_px = 8.f, gain = 1.f, soft = .02f, drift = 20.f;', motes)
        self.assertIn('{"MAX_PX", &FogMoteTuning::max_px, fog_mote_size_min, 64.f}', motes)
        # Absent under the stored range the DLL takes the same default (1300,3,128, MAX_PX 8); an invalid value stays off.
        self.assertIn('constexpr unsigned fog_mote_default_count = 1300;', motes)
        self.assertIn('constexpr float fog_mote_default_size = 3.f, fog_mote_default_streak = 128.f;', motes)
        self.assertIn('float max_px = 8.f,', motes)
        self.assertIn('} else if(motes_length||volumetric_fog_range_stored){', capture)
        self.assertIn('unsigned long n=renderer::fog_mote_default_count;float values[2]{renderer::fog_mote_default_size,renderer::fog_mote_default_streak};bool valid=true;', capture)
        self.assertIn(' source=%s",', capture); self.assertNotIn('key=ctrl_alt_f11', capture); self.assertIn('motes_length?"env":"default"', capture)
        self.assertIn('volumetric_fog_motes_mode enabled=0 invalid=1 reason=%s",valid?"out_of_range":"unparsable"', capture)

    def test_dust_motes_stage_and_frame_row(self):
        # fog-dust-motes.md: the stage is the transaction's last, after the repair and only with the latched toggle; its
        # resources are created at prepare_density (never on a draw path); the frame row carries its fields only with the option.
        source = (ROOT / 'src/renderer/fog_pass.cpp').read_text()
        execute = extract_function(source, 'HRESULT FogPass::execute(')
        self.assertLess(execute.index('r.applied=record(FogStage::Repair,quad(width_,height_));'), execute.index('if(mote_stage&&r.applied&&SUCCEEDED(r.operation)){'))
        self.assertLess(execute.index('if(mote_stage&&r.applied&&SUCCEEDED(r.operation)){'), execute.index('if(opened_here&&!lost_seen){'))
        self.assertIn('const bool motes_wanted=density&&density_config_.dust_motes&&!motes_refused_;', execute)
        self.assertEqual(execute.count('DrawIndexedPrimitive'), 1)
        for forbidden in ('CreateVertexBuffer', 'CreateIndexBuffer', 'CreateVertexShader', 'CreatePixelShader', 'Lock('):
            self.assertNotIn(forbidden, execute)
        resources = extract_function(source, 'HRESULT FogPass::density_resources(')
        self.assertIn('if(density_config_.dust_motes&&!mote_vs_){', resources)
        prepare = extract_function(source, 'HRESULT FogPass::prepare_density(')
        self.assertIn('if(motes_refused_||density_config_.motes.count==0)density_config_.dust_motes=false;', prepare)
        # Released with the targets (Reset, resize, detach), counted in allocations().
        self.assertIn('drop(block_);release_motes();release_quarter();', source)
        self.assertIn('mote_built_count_=n;mote_built_seed_=seed;++allocations_;return S_OK;', source)
        fragment = (ROOT / 'src/proxy/motion_output_fog_inc.h').read_text()
        run = extract_function(fragment, 'void MotionOutput::run_volumetric_fog(')
        self.assertIn('char mote_fields[200]; mote_fields[0] = \'\\0\';\n        if (fog_dust_motes_launch_) {', run)
        for field in ('motes=%u', 'mote_count=%u', 'mote_calls=%u', 'mote_shift_px=%.1f', 'mote_streak=%u', 'mote_shadow=%s', 'mote_refused=%s'):
            self.assertIn(field, run)
        self.assertIn('if (fog_dust_motes_launch_ && in.density) {', run)  # the drift clock is read only with the option
        # The proxy's cut verdict reaches the stage; a cut frame draws no streak whatever the geometric bounds say.
        self.assertIn('in.mote_cut = cut_finished_ && counters_.cut;', run)
        self.assertIn('const bool streak=mote_previous_valid_&&!f.mote_cut&&f.frame==mote_previous_frame_+1&&', source)
        prepare_proxy = extract_function(fragment, 'void MotionOutput::prepare_volumetric_fog_density(')
        self.assertIn('fog_dust_motes_refused device=%llu frame=%llu reason=%s fallback=fog_without_motes', prepare_proxy)

    def test_look_option_and_variable_are_retired(self):
        # 2026-09-22: the presets L0/L1/L3 are gone, the former L2 is the only look, and there is no selector.
        stored = ('--volumetric-fog', '0.03', '--volumetric-fog-cards', 'replace', '--volumetric-fog-range', 'stored')
        status, output, error = self.launch(*self.BASE, *stored)
        self.assertEqual(status, 0, error)
        self.assertNotIn('X3M_VOLUMETRIC_FOG_LOOK', output)  # the launcher no longer sets the selector at all
        # Its refusal stub went on 2026-09-25: a plain unknown argument since.
        for extra in (('--volumetric-fog-look', '2'), ('--volumetric-fog-look', '0'), ('--volumetric-fog-look',)):
            status, _, error = self.launch(*self.BASE, *stored, *extra)
            self.assertEqual(status, 2, extra)
            self.assertIn('unrecognized arguments', error)
        self.assertEqual(self.launch(*self.BASE, '--volumetric-fog', '--volumetric-fog-look', '1')[0], 2)
        # An inherited variable is dropped by the launcher and not read by the DLL (its ignore line went on 2026-09-25), and no
        # preset level is left in the source.
        status, output, error = self.launch(*self.BASE, *stored, environment={'X3M_VOLUMETRIC_FOG_LOOK': '1'})
        self.assertEqual(status, 0, error); self.assertNotIn('X3M_VOLUMETRIC_FOG_LOOK', output)
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertNotIn('X3M_VOLUMETRIC_FOG_LOOK"', capture)
        self.assertNotIn('volumetric_fog_look_step', capture)  # the Ctrl+Alt+F11 cycle is gone with the presets
        self.assertNotIn('" L%d"', capture)                    # and so is the overlay L-readout
        look_math = (ROOT / 'src/renderer/fog_look_math.h').read_text()
        for gone in ('fog_look_default', 'fog_look_count', 'fog_look_next', 'jitter_near', 'JITTER_NEAR'):
            self.assertNotIn(gone, look_math, gone)
        # The look's constants are baked since 2026-09-26: no X3M_FOG_LOOK_<NAME> read and no override table.
        self.assertNotIn('L"X3M_FOG_LOOK_', capture)
        self.assertNotIn('fog_look_fields', look_math)
        self.assertIn('volumetric_fog_look_mode look=single constants=baked', capture)
        self.assertNotIn('PENUMBRA', look_math)

    def test_ambient_occlusion_options_are_removed(self):
        # GTAO/SSAO left the source on 2026-09-22 (cleanup batch 5): argparse refuses every former
        # option as unrecognized, and the dry-run JSON forwards no AO variable.
        for removed in (('--ambient-occlusion',), ('--ao-radius', '3'), ('--ao-strength', '0.3'), ('--ao-debug',), ('--ao-timing',)):
            with self.subTest(removed=removed):
                status, _, error = self.launch(*self.BASE, *removed)
                self.assertEqual(status, 2)
                self.assertIn('unrecognized arguments', error)
                self.assertIn(removed[0], error)
        status, output, error = self.launch(*self.BASE)
        self.assertEqual(status, 0, error)
        self.assertNotIn('X3M_AMBIENT_OCCLUSION', output)
        self.assertNotIn('X3M_AO_', output)

    def test_dependencies_and_ranges(self):
        for missing in ('--taa', '--hdr', '--shadow-replay-depth'):
            with self.subTest(missing=missing):
                status, _, _ = self.launch(*(a for a in self.BASE if a != missing), '--volumetric-fog')
                self.assertEqual(status, 2)
        for dependent in (('--volumetric-fog-cards', 'replace'), ('--volumetric-fog-everywhere',), ('--volumetric-fog-timing',)):  # the last two: removed
            with self.subTest(dependent=dependent):
                self.assertEqual(self.launch(*self.BASE, *dependent)[0], 2)
        # Removed option: argparse refuses it as unrecognized, it does not pass silently.
        status, _, error = self.launch(*self.BASE, '--volumetric-fog', '0.02', '--volumetric-fog-anisotropy', '0.6')
        self.assertEqual(status, 2)
        self.assertIn('unrecognized arguments', error)
        for bad in (('--volumetric-fog', '0.11'), ('--volumetric-fog', '-0.01'), ('--volumetric-fog', 'nan')):
            with self.subTest(bad=bad):
                self.assertEqual(self.launch(*self.BASE, *bad)[0], 2)


class FogWiringTests(unittest.TestCase):
    def test_production_wiring(self):
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        fragment = (ROOT / 'src/proxy/motion_output_fog_inc.h').read_text()
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        # Scene end: after the sun-shadow apply, before the HDR resolve; the copy fallback too; both Reset edges; the card latch at the PS bind.
        hook = motion[motion.index('void MotionOutput::scene_end_hook('):]
        self.assertLess(hook.index('run_sun_shadow_apply();'), hook.index('if (fog_requested_) run_volumetric_fog();'))
        self.assertLess(hook.index('if (fog_requested_) run_volumetric_fog();'), hook.index('resolve_hdr(SceneEndSource::Hook);'))
        self.assertIn('if (bloom && fog_requested_) run_volumetric_fog();', motion)
        self.assertIn('if (fog_) taa_call([&] { fog_->before_reset(); });', motion)
        self.assertIn('fog_->after_reset(result);', motion)
        self.assertIn('if (fog_requested_ && shadow_.ps_hash == renderer::fog_card_pixel_hash) fog_latch_.card(frame_);', motion)
        self.assertIn('#include "motion_output_fog_inc.h"', motion)
        # The off option: every call site and the latch are behind fog_requested_ on their own line.
        sites = [line for line in motion.splitlines() if 'run_volumetric_fog();' in line or 'fog_latch_.card(' in line]
        self.assertEqual(len(sites), 3)
        self.assertTrue(all('fog_requested_' in line for line in sites), sites)
        # Allocation failure: disabled for the session with one line; the sun from the tracked light, a logged fallback otherwise.
        self.assertIn('renderer::fog_failure_action(device_lost, out.failed == renderer::FogStage::Targets, fog_failures_)', fragment)
        self.assertIn('if (!attached) { fog_attach_failed_ = true; fog_->release_density_worker(); }', fragment)
        self.assertIn('fog_failures_ = 0; fog_attach_failed_ = false;', motion[motion.index('void MotionOutput::before_reset()'):motion.index('void MotionOutput::after_reset(')])
        self.assertIn('if (cut_finished_ && counters_.cut) fog_latch_.cut(frame_);', fragment)
        self.assertIn('volumetric_fog_disabled device=%llu frame=%llu reason=%s result=%08lx session=1', fragment)
        self.assertIn('renderer::fog_sun_radiance(point_sun_sample_.colour, q.sun_radiance)', fragment)
        self.assertIn('std::memcpy(out->colour,best.rgb,12);', (ROOT / 'src/proxy/sun_light_poll.cpp').read_text())
        # No Ctrl+Alt+F9/F10 keys since 2026-09-26: the fog runs at its launch state; the toggle and step are fixture seams.
        for absent in ('GetAsyncKeyState(VK_F9)', 'GetAsyncKeyState(VK_F10)', 'volumetric_fog_toggle(', 'volumetric_fog_step(', 'keys=ctrl_alt'):
            self.assertNotIn(absent, capture)
        self.assertIn('ctx.motion_output.volumetric_fog_begin_frame();', capture)
        self.assertIn('volumetric_fog_requested=asked && motion_output_requested && taa_requested && hdr_requested && fog_replay && fog_cascade_list && volumetric_fog_strength>0.f;', capture)
        self.assertIn('src/renderer/fog_pass.cpp', (ROOT / 'CMakeLists.txt').read_text())


if __name__ == '__main__':
    unittest.main()
