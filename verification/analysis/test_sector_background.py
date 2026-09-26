"""Bounded sector diagnostic: synthetic memory and exact production wrapper; no Wine/game."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from verification.analysis.test_capture_bloom_lifetime import extract_function
from verification.analysis import test_volumetric_fog
from source_text import source_text

ROOT = Path(__file__).resolve().parents[2]


class SectorBackgroundTests(unittest.TestCase):
    def compile_run(self, filename, fragment=False):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix='x3-sector-background-') as directory:
            directory = Path(directory)
            if fragment:
                (directory / 'sector_background_context_under_test_inc.h').write_text(
                    extract_function(source_text(ROOT / 'src/proxy/capture.cpp'), 'void sector_background_context('))
            exe = directory / 'host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                    '-I', str(ROOT / 'src/proxy'), '-I', str(directory),
                                    str(ROOT / 'verification/probe' / filename), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('failures=0', run.stdout)
            self.assertEqual(run.stderr, '')
            return run.stdout

    def test_memory_bounds_registry_row_changes_and_camera_floor(self):
        self.assertRegex(self.compile_run('sector_background_host.cpp'), r'checks=\d+ failures=0 sample_bytes=\d+ reads_ready=\d+')

    def test_actual_wrapper_error_cadence_gate_and_zero_work_off(self):
        self.assertIn('checks=22 failures=0', self.compile_run('sector_background_context_host.cpp', fragment=True))  # 19 + the --fog-docked span (3)

    def test_scene_boundary_standalone_and_reset_wiring(self):
        source = source_text(ROOT / 'src/proxy/capture.cpp')
        self.assertIn('if(sector_background_requested)hooked.set(41,begin_scene);', source)
        begin = extract_function(source, 'HRESULT WINAPI begin_scene(')
        self.assertIn('if(SUCCEEDED(hr)&&(sector_background_requested || volumetric_fog_requested))sector_background_context(ctx,true);', begin)
        self.assertIn('CpuCallBoundary cpu;', begin)
        present = extract_function(source, 'HRESULT WINAPI present(')
        self.assertIn('if(sector_background_requested || volumetric_fog_requested)sector_background_context(ctx);', present)
        reset = extract_function(source, 'HRESULT reset_common(')
        self.assertLess(reset.index('ctx.sector_background_evidence.invalidate();'), reset.index('if(ctx.bloom_busy || ctx.motion_output.composition_operation_active())'))
        # No draw hook or render path consumes the diagnostic.
        self.assertEqual(source.count('sector_background_context(ctx);'), 1)
        self.assertEqual(source.count('sector_background_context(ctx,true);'), 1)
        self.assertNotIn('sector_background::sample(', source_text(ROOT / 'src/proxy/motion_output_fog_inc.h'))

    def test_launcher_opt_in_without_rendering_dependencies(self):
        # Part of --debug since the logging tiers (2026-09-26): the DLL reads X3M_SECTOR_BACKGROUND or X3M_DEBUG.
        launcher = test_volumetric_fog.FogLauncherTests()
        status, output, error = launcher.launch('--sector-background')
        self.assertEqual(status, 2)
        self.assertIn('unrecognized arguments', error)
        status, output, error = launcher.launch('--debug')
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_DEBUG": "1"', output)
        self.assertIn('"X3M_VOLUMETRIC_FOG": "0"', output)
        status, output, error = launcher.launch(environment={'X3M_SECTOR_BACKGROUND': '1'})
        self.assertEqual(status, 0, error)
        self.assertNotIn('X3M_SECTOR_BACKGROUND', output)


if __name__ == '__main__':
    unittest.main()
