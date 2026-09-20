import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).parents[2]
BAKER = ROOT / 'tools/build/bake_fog_fields.py'


class FogFieldAssetsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name)
        cls.a, cls.b = cls.root/'a', cls.root/'b'
        cls.first = subprocess.run(['python3', str(BAKER), '--output-dir', str(cls.a)],
                                   cwd=ROOT, text=True, capture_output=True, check=True)
        subprocess.run(['python3', str(BAKER), '--output-dir', str(cls.b)],
                       cwd=ROOT, text=True, capture_output=True, check=True)
        cls.manifest = json.loads((cls.a/'manifest.json').read_text())

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_exact_recipe_hashes_and_packet_sizes(self):
        rows = {row['name']: row for row in self.manifest['profiles']}
        self.assertEqual(self.manifest['numpy'], '2.0.2')
        self.assertEqual(self.manifest['prng'], 'numpy.random.default_rng/PCG64')
        self.assertEqual(rows['bluewell']['decoded_sha256'], '4529497a5e1feda3276b334d6aaef3ba741460e4e4b539aaaf7df74cbfa7261d')
        self.assertEqual(rows['foggreenoutlands']['decoded_sha256'], 'd0342a05fe0e0421c980bb46bec72a765617f9bd043d84d134928a65d3dc0b0c')
        self.assertEqual((rows['bluewell']['nonzero_texels'], rows['bluewell']['runs'], rows['bluewell']['packet_bytes']),
                         (258371, 53509, 2281004))
        self.assertEqual((rows['foggreenoutlands']['nonzero_texels'], rows['foggreenoutlands']['runs'], rows['foggreenoutlands']['packet_bytes']),
                         (517967, 86157, 4488364))

    def test_rerun_is_byte_deterministic(self):
        for name in ('bluewell.fogbin','foggreenoutlands.fogbin','fog_field_assets_metadata_inc.h',
                     'fog_field_assets_resource_inc.h','manifest.json'):
            self.assertEqual(hashlib.sha256((self.a/name).read_bytes()).digest(),
                             hashlib.sha256((self.b/name).read_bytes()).digest(), name)

    def test_decoder_fixture_and_portable_compile(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        executable = self.root/'fog-field-assets-fixture'
        command = [compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-I', str(ROOT/'src/renderer'), '-I', str(self.a),
                   str(ROOT/'src/renderer/fog_field_assets.cpp'),
                   str(ROOT/'verification/probe/fog_field_assets_fixture.cpp'), '-o', str(executable)]
        subprocess.run(command, cwd=ROOT, text=True, capture_output=True, check=True)
        run = subprocess.run([str(executable), str(self.a/'bluewell.fogbin'),
                              str(self.a/'foggreenoutlands.fogbin')],
                             cwd=ROOT, text=True, capture_output=True, check=True)
        self.assertIn('PASS fog_field_assets decoder=2 corruptions_per_profile=11 allocation=1 atomic=1 independent_fullscan=2', run.stdout)
        self.assertIn('decoded_fnv1a=ffe40c913d06714f', run.stdout)
        self.assertIn('decoded_fnv1a=e446bf23796869c6', run.stdout)

    def test_i686_decoder_and_resources_cross_compile_when_available(self):
        compiler = shutil.which('i686-w64-mingw32-g++')
        windres = shutil.which('i686-w64-mingw32-windres')
        if not compiler or not windres:
            self.skipTest('MinGW cross tools unavailable')
        obj = self.root/'fog-field-assets.o'
        subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-msse2', '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2',
                        '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-I', str(ROOT/'src/renderer'),
                        '-I', str(self.a), '-c', str(ROOT/'src/renderer/fog_field_assets.cpp'), '-o', str(obj)],
                       cwd=ROOT, text=True, capture_output=True, check=True)
        template = (ROOT/'cmake/fog_field_assets.rc.in').read_text()
        configured = template.replace('@X3M_FOG_BLUEWELL_BIN@', str(self.a/'bluewell.fogbin'))
        configured = configured.replace('@X3M_FOG_FOGGREENOUTLANDS_BIN@', str(self.a/'foggreenoutlands.fogbin'))
        rc = self.root/'fog_field_assets.rc'; rc.write_text(configured)
        resource_obj = self.root/'fog-field-assets-resource.o'
        subprocess.run([windres, '-I', str(self.a), '-i', str(rc), '-o', str(resource_obj)],
                       cwd=ROOT, text=True, capture_output=True, check=True)
        self.assertGreater(resource_obj.stat().st_size, 6_769_480)

    def test_resource_template_and_integration_module_are_explicit(self):
        template = (ROOT/'cmake/fog_field_assets.rc.in').read_text()
        module = (ROOT/'cmake/FogField.cmake').read_text()
        self.assertIn('RCDATA', template)
        self.assertIn('enable_language(RC)', module)
        self.assertIn('NumPy 2.0.2', module)
        self.assertIn('-DPython3_EXECUTABLE=/absolute/path/to/python3', module)
        self.assertIn('x3m_add_fog_field_assets', module)
        self.assertNotIn('verification/', (ROOT/'tools/build/bake_fog_fields.py').read_text())

    def test_cmake_checks_the_selected_python_dependency(self):
        cmake = shutil.which('cmake')
        compiler = shutil.which('i686-w64-mingw32-g++')
        windres = shutil.which('i686-w64-mingw32-windres')
        if not cmake or not compiler or not windres:
            self.skipTest('requires CMake and MinGW cross tools')
        source = self.root/'cmake-dependency-source'
        source.mkdir()
        (source/'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.20)\n'
            'project(fog_field_dependency LANGUAGES CXX)\n'
            f'include("{(ROOT/"cmake/FogField.cmake").as_posix()}")\n')
        common = [cmake, '-S', str(source),
                  f'-DCMAKE_TOOLCHAIN_FILE={ROOT/"cmake/mingw-i686.cmake"}']
        good = subprocess.run(common + ['-B', str(self.root/'cmake-good'),
                              f'-DPython3_EXECUTABLE={sys.executable}'],
                              text=True, capture_output=True)
        self.assertEqual(good.returncode, 0, good.stdout + good.stderr)

        unsupported = self.root/'python-without-qualified-numpy'
        unsupported.write_text('#!/bin/sh\nexit 7\n')
        unsupported.chmod(0o755)
        bad = subprocess.run(common + ['-B', str(self.root/'cmake-bad'),
                             f'-DPython3_EXECUTABLE={unsupported}'],
                             text=True, capture_output=True)
        diagnostic = bad.stdout + bad.stderr
        self.assertNotEqual(bad.returncode, 0)
        self.assertIn('requires importable NumPy 2.0.2', diagnostic)
        self.assertIn(str(unsupported), diagnostic)
        self.assertIn('-DPython3_EXECUTABLE=/absolute/path/to/python3', diagnostic)


if __name__ == '__main__':
    unittest.main()
