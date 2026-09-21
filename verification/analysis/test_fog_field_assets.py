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
        # Frozen Run53 packets: both original visuals and packet identities stay exact.
        self.assertEqual(rows['bluewell']['resource_sha256'], 'fea1a4bf3842b417007af27c6cc25092c3ac8e815660b02bc3ac410953f1f372')
        self.assertEqual(rows['foggreenoutlands']['resource_sha256'], '014d586109e5968dbd0358969ab7082e7fc3e670335a59d889f6e2ba649d87f8')
        self.assertEqual((rows['bluewell']['nonzero_texels'], rows['bluewell']['runs'], rows['bluewell']['packet_bytes']),
                         (258371, 53509, 2281004))
        self.assertEqual((rows['foggreenoutlands']['nonzero_texels'], rows['foggreenoutlands']['runs'], rows['foggreenoutlands']['packet_bytes']),
                         (517967, 86157, 4488364))

    def test_provisional_profiles_have_bounded_authored_prior_and_unique_resources(self):
        rows=self.manifest['profiles']
        self.assertEqual(len({row['profile_id'] for row in rows}),14)
        self.assertEqual(len({row['resource_id'] for row in rows}),14)
        for row in rows:
            self.assertEqual(row['decoded_bytes'],17_846_400)
            if row['name'] not in ('bluewell','foggreenoutlands'):
                self.assertEqual(row['density_status'],'provisional_artistic')
                self.assertEqual(row['occupancy'],.12)
                self.assertEqual(row['base_sigma'],2.5e-6)

    def test_rerun_is_byte_deterministic(self):
        self.assertEqual({p.name for p in self.a.iterdir()},{p.name for p in self.b.iterdir()})
        for name in (p.name for p in self.a.iterdir()):
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
        run = subprocess.run([str(executable), *(str(self.a/(row['name']+'.fogbin')) for row in self.manifest['profiles'])],
                             cwd=ROOT, text=True, capture_output=True, check=True)
        self.assertIn('PASS fog_field_assets decoder=14 corruptions_per_profile=11 allocation=1 atomic=1 independent_fullscan=14', run.stdout)
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
        rc = self.root/'fog_field_assets.rc'; rc.write_text(template)
        resource_obj = self.root/'fog-field-assets-resource.o'
        subprocess.run([windres, '-I', str(self.a), '-i', str(rc), '-o', str(resource_obj)],
                       cwd=ROOT, text=True, capture_output=True, check=True)
        self.assertGreater(resource_obj.stat().st_size, sum(row['resource_bytes'] for row in self.manifest['profiles']))

    def test_resource_template_and_integration_module_are_explicit(self):
        template = (ROOT/'cmake/fog_field_assets.rc.in').read_text()
        module = (ROOT/'cmake/FogField.cmake').read_text()
        self.assertIn('fog_field_assets_entries.rc', template)
        rc_rows=(self.a/'fog_field_assets_entries.rc').read_text()
        self.assertEqual(rc_rows.count(' RCDATA '),14)
        self.assertEqual(len(self.manifest['profiles']),14)
        inventory=subprocess.check_output([sys.executable,str(BAKER),'--list-profiles'],text=True).strip().split(';')
        self.assertEqual(inventory,[row['name'] for row in self.manifest['profiles']])
        for row in self.manifest['profiles']:
            self.assertIn(f'"{row["name"]}.fogbin"',rc_rows)
            self.assertEqual(hashlib.sha256((self.a/(row['name']+'.fogbin')).read_bytes()).hexdigest(),row['resource_sha256'])
        self.assertIn('enable_language(RC)', module)
        self.assertIn('NumPy 2.0.2', module)
        self.assertIn('-DPython3_EXECUTABLE=/absolute/path/to/python3', module)
        self.assertIn('x3m_add_fog_field_assets', module)
        self.assertNotIn('verification/', (ROOT/'tools/build/bake_fog_fields.py').read_text())

    def test_tracked_family_chroma_matches_the_packets(self):
        # Independent of tools/build/fog_family_chroma.py and of fog_distance_replay: raw run decode, atlas interior.
        import re, struct
        import numpy as np
        tracked = {int(i): tuple(np.float32(v) for v in (r, g, b)) for i, r, g, b in
                   re.findall(r'^\{(\d+)u, \{([0-9.e-]+)f, ([0-9.e-]+)f, ([0-9.e-]+)f\}\}', (ROOT/'src/renderer/fog_family_chroma_inc.h').read_text(), re.M)}
        self.assertEqual(len(tracked), len(self.manifest['profiles']))
        for row in self.manifest['profiles']:
            data = (self.a/(row['name']+'.fogbin')).read_bytes()
            header = struct.unpack_from('<8sIIIIIIIIIQI', data)
            width, height, runs = header[5], header[6], header[9]
            out = bytearray(); at = header[2]
            for _ in range(runs):
                word, = struct.unpack_from('<I', data, at); at += 4; count = word & 0x7fffffff
                if word & 0x80000000: out += data[at:at+8*count]; at += 8*count
                else: out += bytes(8*count)
            atlas = np.frombuffer(bytes(out), np.float16).reshape(height, width, 4)
            total = np.zeros(4, np.float64)
            for z in range(128):
                y, x = (z//12)*130+1, (z % 12)*130+1
                total += atlas[y:y+128, x:x+128].astype(np.float64).sum((0, 1))
            chroma = (total[:3]/total[3]).astype(np.float32)
            self.assertEqual(tuple(chroma), tracked[row['profile_id']], row['name'])
        self.assertEqual(tuple(round(float(v), 4) for v in tracked[1]), (0.0516, 0.2695, 1.0))  # the bridge's bluewell line
        production = (ROOT/'src/renderer/fog_pass.cpp').read_text()
        self.assertIn('#include "fog_family_chroma_inc.h"', production)
        self.assertNotIn('half_to_float', production)  # no runtime scan of the decoded atlas

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
