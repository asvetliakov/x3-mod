"""Serialised GPU pass timing (--gpu-sync-timing; docs/architecture/engine-frame-time.md,
"GPU sync timing"): host execution of the D3D-free core (tick conversion, histogram
buckets, percentiles, the per-frame pair rules, window rotation), the launcher option
(--dry-run only, never a launch) with its vanilla refusal, and the production wiring
the Wine fixture cannot see. No Wine, game or DLL build."""
import contextlib
import hashlib
import importlib.util
import io
import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'src/renderer/gpu_sync_timing_core.h'


def load_manage():
    spec = importlib.util.spec_from_file_location('gpu_sync_timing_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class GpuSyncTimingCore(unittest.TestCase):
    def test_core_ring_arithmetic(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        fixture = ROOT / 'verification/probe/gpu_sync_timing_core_fixture.cpp'
        with tempfile.TemporaryDirectory(prefix='x3-gpu-sync-timing-') as temporary:
            for name, flags in [('release', ['-O2']), ('sanitized', ['-O1', '-g', '-fsanitize=address,undefined'])]:
                with self.subTest(mode=name):
                    executable = Path(temporary) / name
                    build = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags, str(fixture), '-o', str(executable)],
                                           capture_output=True, text=True, timeout=180)
                    self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                    run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                    self.assertEqual(run.stdout, 'gpu_sync_timing_core checks=36 failures=0\n')

    def test_core_is_integer_only_without_d3d_or_heap(self):
        core = CORE.read_text()
        code = re.sub(r'//[^\n]*', '', core)
        for forbidden in ('d3d9.h', 'windows.h', 'IDirect3D', 'HRESULT', 'new ', 'malloc', 'std::vector', 'std::string', 'float', 'double'):
            self.assertNotIn(forbidden, code, forbidden)


class GpuSyncTimingLauncher(unittest.TestCase):
    def launch(self, directory, *args, inherited=None, vanilla=False):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        if not vanilla:
            (game / 'd3d9.dll').write_bytes(b'proxy')
            (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', *(['--vanilla'] if vanilla else []), '--game-dir', str(game), *args]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), mock.patch.object(module, 'VOICE_DECODER_REPO', None), \
                mock.patch.dict(module.os.environ, inherited or {}), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, output.getvalue(), error.getvalue()
        return 0, output.getvalue(), error.getvalue()

    def env(self, directory, *args, **kw):
        code, output, error = self.launch(directory, *args, **kw)
        self.assertEqual(code, 0, error)
        return json.loads(output)['env']

    def test_dry_run_default_off_and_opt_in(self):
        with tempfile.TemporaryDirectory() as directory:
            # Sent only with the option since the logging tiers (2026-09-26); an inherited value is dropped, never forwarded.
            self.assertNotIn('X3M_GPU_SYNC_TIMING', self.env(directory))
            self.assertNotIn('X3M_GPU_SYNC_TIMING', self.env(directory, inherited={'X3M_GPU_SYNC_TIMING': '1'}))  # an inherited value cannot switch it on
            baseline = self.env(directory)
            opted = self.env(directory, '--gpu-sync-timing')
            self.assertEqual({k: v for k, v in opted.items() if baseline.get(k) != v}, {'X3M_GPU_SYNC_TIMING': '1'})

    def test_refused_under_vanilla(self):
        with tempfile.TemporaryDirectory() as directory:
            code, _, error = self.launch(directory, '--gpu-sync-timing', vanilla=True)
            self.assertEqual(code, 2)
            self.assertIn('--gpu-sync-timing cannot be combined with --vanilla', error)
            self.assertNotIn('X3M_GPU_SYNC_TIMING', self.env(directory, vanilla=True))

    def test_help_says_it_serialises_for_one_flight(self):
        source = (ROOT / 'tools/manage.py').read_text()
        start = source.index("'--gpu-sync-timing'")
        help_text = source[start:source.index('\n', start)]
        for phrase in ('X3M_GPU_SYNC_TIMING=1', 'default off', 'refused with --vanilla', 'one flight', 'serialises CPU and GPU'):
            self.assertIn(phrase, help_text)


class GpuSyncTimingRunner(unittest.TestCase):
    def test_parse_and_accept(self):
        spec = importlib.util.spec_from_file_location('run_gpu_sync_timing', ROOT / 'verification/probe/run_gpu_sync_timing.py')
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)
        lines = ['DEVICE adapter=x driver=y', 'PROBE event=00000000',
                 'SOFTFAIL kind=unsupported available=0 reason=event_unsupported result=8876086a references=0',
                 'SOFTFAIL kind=partial available=0 reason=create_failed result=8007000e references=0 device=1 baseline=1',
                 'SUPPORT available=1 reason=ok result=00000000 references=28 device_delta=28 queries=28 frequency=10000000']
        for phase, count in (('first', 3), ('after_reset', 1)):
            for window in range(count):
                lines.append(f'WINDOW phase={phase} window={window + 1} frames=1..16 n_frames=16 dropped=0 unclosed=0 dt_n=16 dt_median_us=9000 dt_p90_us=9500')
                lines.append(f'PASS phase={phase} window={window + 1} pass=taa n=16 median_us=120 p90_us=150 wait_median_us=40 session_n=16 session_median_us=120 session_p90_us=150')
        passing = lines + [f'CHECK c{i} PASS' for i in range(runner.EXPECTED_CHECKS)] + [f'RESULT checks={runner.EXPECTED_CHECKS} failures=0 path=measured PASS']
        report = runner.parse('\n'.join(passing))
        runner.accept(report)
        self.assertEqual(runner.summary_of(report)['pass_median_us_per_window'], {'taa': [120, 120, 120, 120]})
        short = lines + [f'CHECK c{i} PASS' for i in range(runner.EXPECTED_CHECKS - 1)] + [f'RESULT checks={runner.EXPECTED_CHECKS - 1} failures=0 path=measured PASS']
        with self.assertRaises(AssertionError):
            runner.accept(runner.parse('\n'.join(short)))
        failed = passing[:-1] + ['CHECK extra FAIL', f'RESULT checks={runner.EXPECTED_CHECKS + 1} failures=1 path=measured FAIL']
        with self.assertRaises(AssertionError):
            runner.accept(runner.parse('\n'.join(failed)))


class GpuSyncTimingWiring(unittest.TestCase):
    # Production begin / end / scoped (Span) sites per pass. Every pass has a begin and an
    # end site (a Span is both); Engine has two ends (the hook scene end and the copy
    # fallback), SunApply one Span (the cascades; the single map's went on 2026-09-25), Bloom two pairs (prepare, commit); the five taa_* sub-passes
    # one pair each inside TemporalPass::run; the three fog_* sub-passes one Span each inside FogPass::execute; the three
    # taa_mask_* draws one Span (its pass chosen per loop iteration) inside TemporalPass::run's mask loop.
    SITES = {'Scene': (1, 1, 0), 'Engine': (1, 2, 0), 'ShadowDepth': (0, 0, 1), 'SunApply': (0, 0, 1), 'Retention': (0, 0, 1),
             'FogFill': (0, 0, 1), 'FogRoute': (1, 1, 0), 'Motes': (0, 0, 1), 'Taa': (1, 1, 0), 'HdrWriteback': (1, 1, 0),
             'Meter': (1, 1, 0), 'HdrReadback': (1, 1, 0), 'Bloom': (2, 2, 0), 'Present': (1, 1, 0),
             'TaaCopy': (1, 1, 0), 'TaaMask': (1, 1, 0), 'TaaBox': (1, 1, 0), 'TaaResolve': (1, 1, 0), 'TaaDisplay': (1, 1, 0),
             'FogMarch': (0, 0, 1), 'FogComposite': (0, 0, 1), 'FogRepair': (0, 0, 1),
             'TaaMaskTests': (0, 0, 1), 'TaaMaskX': (0, 0, 1), 'TaaMaskY': (0, 0, 1)}

    def test_every_pass_has_begin_and_end_sites_in_production(self):
        sources = ''.join((ROOT / path).read_text() for path in (
            'src/proxy/capture.cpp', 'src/proxy/motion_output.cpp', 'src/proxy/motion_output_fog_inc.h', 'src/renderer/hdr_pass.cpp', 'src/renderer/fog_pass.cpp',
            'src/renderer/temporal_pass.cpp'))
        names = re.findall(r'^\s+(\w+)(?: = 0)?,\s+//', CORE.read_text().split('enum Pass')[1].split('pass_count')[0], re.M)
        self.assertEqual(names, list(self.SITES))
        for name in names:
            begins = len(re.findall(rf'begin\(gpu_sync_timing::{name}\)|gpu_sync_timing::{name},true\)', sources))
            ends = len(re.findall(rf'end\(gpu_sync_timing::{name}\)|gpu_sync_timing::{name},false\)', sources))
            spans = len(re.findall(rf'Span \w+\([^;]*gpu_sync_timing::{name}\)', sources))
            self.assertEqual((begins, ends, spans), self.SITES[name], name)
            self.assertGreaterEqual(begins + spans, 1, name)
            self.assertGreaterEqual(ends + spans, 1, name)

    def test_lifetime_and_off_cost(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        # Off: no object, the environment read once; the final-release accounting counts the queries.
        self.assertIn('gpu_sync_timing_requested=x3m::config::get(L"X3M_GPU_SYNC_TIMING",setting,32)==1', capture)
        self.assertIn('if(gpu_sync_timing_requested)gpu_sync_attach(hooked,d);', capture)
        self.assertEqual(capture.count('gpu_sync_references(ctx)'), 2)
        self.assertIn('gpu_sync_release(ctx); ctx.bloom.shutdown();', capture)
        self.assertIn('gpu_sync_before_reset(ctx);', capture)
        self.assertIn('gpu_sync_after_reset(ctx,hr);', capture)
        self.assertIn('if(!ctx.gpu_sync->available()){ctx.gpu_sync.reset();return;}', capture)
        # The failure cut-off never releases inside a pass: the owner defers, the Present hook
        # releases under BloomOperation (no final-release accounting while a query's final
        # Release re-enters release_device), and release() zeroes the references first.
        present_helper = capture[capture.index('void gpu_sync_present(Device& ctx) {'):]
        present_helper = present_helper[:present_helper.index('\n}\n')]
        self.assertIn('{BloomOperation internal(ctx);ctx.gpu_sync->release_deferred();}', present_helper)
        owner_source = (ROOT / 'src/renderer/gpu_sync_timing.cpp').read_text()
        mark = owner_source[owner_source.index('void GpuSyncTiming::mark('):owner_source.index('bool GpuSyncTiming::frame(')]
        self.assertNotIn('release()', mark)
        self.assertIn('release_pending_ = true', mark)
        release = owner_source[owner_source.index('void GpuSyncTiming::release()'):owner_source.index('void GpuSyncTiming::before_reset()')]
        self.assertLess(release.index('references_ = 0;'), release.index('->Release()'))
        # A lost device is refused before any spin; the timeout cut-off is sticky.
        self.assertIn('native_[TestCooperativeLevel]', mark)
        self.assertIn('if (tripped_) {', owner_source)
        present = capture[capture.index('HRESULT WINAPI present('):]
        order = [present.index(s) for s in ('gpu_sync_mark(ctx,gpu_sync_timing::Present,true);', 'gpu_sync_mark(ctx,gpu_sync_timing::Scene,false);',
                                            'const HRESULT hr=fn(d,a,b,w,r);', 'gpu_sync_present(ctx);', '++ctx.frame;')]
        self.assertEqual(order, sorted(order))
        owner = (ROOT / 'src/renderer/gpu_sync_timing.cpp').read_text()
        self.assertIn('D3DQUERYTYPE_EVENT, nullptr', owner)       # documented support probe
        self.assertIn('Issue(D3DISSUE_END)', owner)
        self.assertIn('GetData(&done, sizeof done, D3DGETDATA_FLUSH)', owner)
        self.assertIn('PreserveCpuState guard;', owner)


if __name__ == '__main__':
    unittest.main()
