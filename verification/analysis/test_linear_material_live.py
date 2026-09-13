"""Host execution of production live material control flow with scripted COM.

This checks registry/fallback/shadow lifetime, not shader math, Windows ABI or GPU
behavior. Functions are extracted unchanged; external APIs are explicit doubles.
"""
from pathlib import Path
import contextlib
import importlib.util
import io
import os
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from verification.analysis.test_capture_bloom_lifetime import extract_function

ROOT = Path(__file__).resolve().parents[2]


class LinearMaterialLiveTests(unittest.TestCase):
    def test_production_control_flow(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        source = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        signatures = [
            'unsigned MotionOutput::device_references() const noexcept',
            'void MotionOutput::release_resources() noexcept',
            'void MotionOutput::configure_linear_materials(bool requested, const renderer::LinearMaterialConfig& config) noexcept',
            'void MotionOutput::register_vertex_shader(',
            'void MotionOutput::register_pixel_shader(',
            'void MotionOutput::set_vertex_shader(',
            'void MotionOutput::set_pixel_shader(',
            'void MotionOutput::set_sampler_state(',
            'void MotionOutput::resync_samplers() noexcept',
            'unsigned MotionOutput::linear_material_refusal() const noexcept',
            'HRESULT MotionOutput::bind_variant_pair(',
            'HRESULT MotionOutput::undo(',
        ]
        with tempfile.TemporaryDirectory(prefix='x3-linear-material-live-') as directory:
            path = Path(directory)
            capture = (ROOT / 'src/proxy/capture.cpp').read_text()
            start = capture.index('    const bool material_requested=GetEnvironmentVariableW')
            end = capture.index('    bloom_requested=GetEnvironmentVariableW', start)
            environment = 'void configure_environment() { wchar_t setting[32]{};\n' + capture[start:end] + '}\n'
            (path / 'linear_material_live_under_test_inc.h').write_text('\n\n'.join(extract_function(source, sig) for sig in signatures) + '\n' + environment)
            executable = path / 'fixture'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', directory,
                                    str(ROOT / 'verification/probe/linear_material_live_fixture.cpp'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('failures=0', run.stdout)
            self.assertEqual(run.stderr, '')

    def launch(self, *args, environment=None):
        spec = importlib.util.spec_from_file_location('linear_material_manage', ROOT / 'tools/manage.py')
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

    def test_cli_dependencies_and_gain_bounds(self):
        valid = ('--motion-output', '--hdr', '--hdr-tonemap', '--linear-materials')
        rejected = [('--linear-materials',), ('--motion-output', '--hdr', '--linear-materials'),
                    (*valid, '--hdr-decode', 'none'), (*valid, '--hdr-decode', 'srgb')]
        for option in ('--material-direct-gain', '--material-emissive-gain', '--lightmap-emissive-gain'):
            rejected.append((option, '1'))
            for value in ('-1', '16.01', 'nan', 'inf', '-inf'):
                rejected.append((*valid, f'{option}={value}'))
        for args in rejected:
            with self.subTest(args=args):
                code, _, _ = self.launch(*args)
                self.assertEqual(code, 2)
        for value in ('0', '1', '4', '16'):
            code, output, error = self.launch(*valid, '--material-direct-gain', value, '--material-emissive-gain', value, '--lightmap-emissive-gain', value)
            self.assertEqual(code, 0, error)
            self.assertIn('X3M_LINEAR_MATERIALS', output)
            self.assertIn(f'"X3M_MATERIAL_DIRECT_GAIN": "{float(value)}"', output)
        code, _, error = self.launch(*valid, '--hdr-decode', 'pow22')
        self.assertEqual(code, 0, error)

    def test_cli_clears_inherited_feature_and_gains(self):
        code, output, error = self.launch(environment={'X3M_LINEAR_MATERIALS': '1', 'X3M_MATERIAL_DIRECT_GAIN': '16'})
        self.assertEqual(code, 0, error)
        self.assertIn('"X3M_LINEAR_MATERIALS": "0"', output)
        self.assertIn('"X3M_MATERIAL_DIRECT_GAIN": "1.0"', output)


if __name__ == '__main__':
    unittest.main()
