"""Host tests of the FL loading quintet (src/engine/fl/gz_buffer_fl.cpp, crypt_cache_fl.cpp, mesh_adjacency_sites_fl.h,
the FL branch of src/proxy/loading_trace.cpp, and src/engine/fl/resource_reader_fl.cpp: resource_read and dat_handles).

docs/architecture/platform-portability.md, "2026-11-03: loading quintet on FL". (a) The native fixture
verification/probe/fl_loading.cpp (MinGW i686, natively through WSL interop, no Wine) maps the installed X3FL.exe,
relocates it to a private base and installs the production gz_buffer and crypt_cache FL transactions over the real
relocated bytes: every gz window change refused; the six zlib slots claimed and read back with LastError preserved; a
buffered read byte-identical to a synthetic stream; the gzclose share with the music keep in both install orders; a
partial-install rollback; the crypt cache's admission from the checker's return sites (first pass native, second a hit),
an unadmitted sibling acquire retiring the cache, LastError preserved. (a') The native fixture
verification/probe/fl_resource_reader.cpp (same host, the game's zlib1.dll copied next to it) runs the real relocated
FL reader as the native route and the production reader/pool install over it (docs/reverse-engineering/fl/
resource-reader.md, "Port (2026-11-03)"). (b) The four read-only site verifiers on the exe and on mutated copies. (c) The
generated identity header is reproducible. (d) Edition wiring.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

import fl_music_sites_common as c
import verify_gz_buffer_sites_fl as gz_verifier
import verify_crypt_cache_sites_fl as crypt_verifier
import verify_mesh_adjacency_sites_fl as mesh_verifier
import verify_resource_reader_sites_fl as reader_verifier

ROOT = Path(__file__).resolve().parents[2]
EXE = c.DEFAULT_EXE
COMMON = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
          '-mincoming-stack-boundary=2', '-msse2', '-mfpmath=sse', '-DX3M_EDITION_FL=1', '-DX3M_GZ_BUFFER_FL_FIXTURE', '-DX3M_CRYPT_CACHE_FL_FIXTURE', '-DX3M_LOADING_FL_FIXTURE']
NOSSE = ['-mno-sse', '-mno-mmx', '-mfpmath=387']


def available():
    return bool(shutil.which('i686-w64-mingw32-g++')) and Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists()


def build(tmp):
    cxx = shutil.which('i686-w64-mingw32-g++')
    tmp = Path(tmp)
    units = [('src/engine/fl/gz_buffer_fl.cpp', ['-fno-exceptions']),
             ('src/engine/fl/crypt_cache_fl.cpp', ['-fno-exceptions', *NOSSE]),
             ('src/proxy/gz_buffer.cpp', []),
             ('src/proxy/crypt_cache.cpp', NOSSE),
             ('src/proxy/engine_patch.cpp', []),
             ('src/engine/fl/executable_identity_fl.cpp', [])]
    objects = []
    for source, flags in units:
        obj = tmp / (Path(source).stem + '.o')
        subprocess.run([cxx, *COMMON, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = tmp / 'fl_loading.exe'
    subprocess.run([cxx, *COMMON, '-static', '-static-libgcc', '-static-libstdc++',
                    str(ROOT / 'verification/probe/fl_loading.cpp'), *objects, '-ladvapi32', '-o', str(exe)], check=True)
    return exe


def build_reader(tmp):
    cxx = shutil.which('i686-w64-mingw32-g++')
    tmp = Path(tmp)
    flags = [f for f in COMMON if not f.startswith('-DX3M_') or f == '-DX3M_EDITION_FL=1'] + \
        ['-DX3M_RESOURCE_READER_FL_FIXTURE']
    units = [('src/engine/fl/resource_reader_fl.cpp', ['-fno-exceptions']),
             ('src/proxy/resource_reader_core.cpp', NOSSE),
             ('src/proxy/engine_patch.cpp', [])]
    objects = []
    for source, extra in units:
        obj = tmp / ('rr_' + Path(source).stem + '.o')
        subprocess.run([cxx, *flags, *extra, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = tmp / 'fl_resource_reader.exe'
    subprocess.run([cxx, *flags, '-static', '-static-libgcc', '-static-libstdc++',
                    str(ROOT / 'verification/probe/fl_resource_reader.cpp'), *objects, '-o', str(exe)], check=True)
    shutil.copyfile(EXE.parent / 'zlib1.dll', tmp / 'zlib1.dll')
    return exe


def windows_path(path):
    return subprocess.run(['wslpath', '-w', str(path)], capture_output=True, text=True, check=True).stdout.strip()


class FlLoadingNative(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='x3-fl-loading-')
        cls.run_ = None
        if available() and EXE.exists():
            exe = build(cls.tmp.name)
            cls.run_ = subprocess.run([str(exe), windows_path(EXE)], text=True, capture_output=True, timeout=300)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        if self.run_ is None:
            self.skipTest('needs i686-w64-mingw32-g++, WSL interop and the installed X3FL.exe')

    def test_fixture_passes(self):
        out = self.run_.stdout
        self.assertEqual(self.run_.returncode, 0,
                         '\n'.join(l for l in out.splitlines() if l.startswith('FAIL'))[:3000] + self.run_.stderr[-800:])
        self.assertGreaterEqual(int(re.search(r'RESULT PASS checks=(\d+) failures=0', out).group(1)), 18)
        cases, refused = map(int, re.search(r'GZFLIPS windows=20 cases=(\d+) refused=(\d+)', out).groups())
        self.assertEqual(cases, refused)
        rows = re.findall(r'^LOG (\S+ .*)$', out, re.M)
        self.assertTrue(all('edition=fl' in r for r in rows if r.startswith(('gz_buffer ', 'crypt_cache ',
                                                                             'loading_hook '))), rows[:4])
        self.assertTrue(any(r.startswith('gz_buffer ') and 'enabled=1' in r and 'close_route=slot' in r for r in rows))
        self.assertTrue(any(r.startswith('crypt_cache ') and 'enabled=1' in r and 'foreign=retire' in r for r in rows))
        self.assertTrue(any(r.startswith('crypt_cache scope=session') and 'foreign_retired=' in r for r in rows))


class FlResourceReaderNative(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='x3-fl-reader-')
        cls.run_ = None
        if available() and EXE.exists() and (EXE.parent / 'zlib1.dll').exists():
            exe = build_reader(cls.tmp.name)
            cls.run_ = subprocess.run([str(exe), windows_path(EXE), windows_path(Path(cls.tmp.name) / 'zlib1.dll')],
                                      text=True, capture_output=True, timeout=300)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        if self.run_ is None:
            self.skipTest('needs i686-w64-mingw32-g++, WSL interop, the installed X3FL.exe and its zlib1.dll')

    def test_fixture_passes(self):
        out = self.run_.stdout
        self.assertEqual(self.run_.returncode, 0,
                         '\n'.join(l for l in out.splitlines() if l.startswith('FAIL'))[:3000] + self.run_.stderr[-800:])
        self.assertGreaterEqual(int(re.search(r'RESULT PASS checks=(\d+) failures=0', out).group(1)), 60)
        cases, refused = map(int, re.search(r'RRFLIPS windows=33 cases=(\d+) refused=(\d+)', out).groups())
        self.assertEqual(cases, refused)
        n, equal = map(int, re.search(r'FAST cases=(\d+) equal=(\d+)', out).groups())
        self.assertEqual(n, equal)
        self.assertRegex(out, r'VERIFY files=(\d+) equal=\1 mismatched=0')
        self.assertIn('CLOSES site_targets=17 pool_close_calls=34 pooled=17 released=17', out)
        self.assertRegex(out, r'ROLLBACK steps=20 rolled_back=20')
        rows = re.findall(r'^LOG (\S+ .*)$', out, re.M)
        self.assertTrue(all('edition=fl' in r for r in rows if r.startswith(('resource_reader', 'dat_handle_pool'))))
        self.assertTrue(any(r.startswith('resource_reader mode=fast installed=1 status=active') and 'input=ecx' in r
                            for r in rows))
        self.assertTrue(any(r.startswith('dat_handle_pool requested=1 installed=1 status=ok') and 'live=18' in r and 'close_sites=17' in r
                            and 'error_flag=0x10' in r for r in rows))
        self.assertTrue(any(r.startswith('resource_reader_metric cumulative=1') for r in rows))


class FlLoadingSites(unittest.TestCase):
    def setUp(self):
        if not EXE.exists():
            self.skipTest('needs the installed X3FL.exe')

    def test_gz_verifier(self):
        self.assertEqual(gz_verifier.verify(EXE)['result'], 'PASS', gz_verifier.verify(EXE)['failed'])

    def test_crypt_verifier(self):
        self.assertEqual(crypt_verifier.verify(EXE)['result'], 'PASS', crypt_verifier.verify(EXE)['failed'])

    def test_mesh_verifier(self):
        self.assertEqual(mesh_verifier.verify(EXE)['result'], 'PASS', mesh_verifier.verify(EXE)['failed'])

    def test_reader_verifier(self):
        report = reader_verifier.verify(EXE)
        self.assertEqual(report['result'], 'PASS', report['failed'])
        self.assertEqual(report['close_sites_vs_codex'], {'only_scan': [], 'only_codex': []})

    def test_reader_verifier_refuses_mutations(self):
        data = EXE.read_bytes()
        im = c.Image(EXE)
        with tempfile.TemporaryDirectory(prefix='x3-fl-reader-mut-') as tmp:
            # a close call's rel32, the entry's third byte, the error flag immediate: each fails the verifier
            for rva, expect in ((0x10696d, 'pool_windows_match_exe'), (0x105f42, 'reader_windows_match_exe'),
                                (0x132c84, 'pool_windows_match_exe')):
                mutated = bytearray(data)
                mutated[im.pe.offset(rva)] ^= 0x01
                path = Path(tmp) / 'X3FL.exe'
                path.write_bytes(bytes(mutated))
                report = reader_verifier.verify(path, sha256=None)
                self.assertEqual(report['result'], 'FAIL')
                self.assertIn(expect, report['failed'])

    def test_header_regenerates(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            'gen_loading', ROOT / 'verification/results/fl-loading/generate_windows.py')
        gen = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(gen)
        text, _ = gen.generate(EXE)
        self.assertEqual(text, gen.OUT.read_text())


def strip(text):
    return re.sub(r'//.*', '', text)


class FlLoadingWiring(unittest.TestCase):
    def test_cmake(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        ap = re.search(r'set\(X3M_AP_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        self.assertIn('src/proxy/gz_buffer.cpp', ap)
        self.assertIn('src/proxy/crypt_cache.cpp', ap)
        fl = cmake[cmake.index('add_library(d3d9 SHARED ${X3M_SHARED_SOURCES}'):cmake.index('x3m_add_fog_field_assets')]
        for needle in ('src/engine/fl/gz_buffer_fl.cpp', 'src/engine/fl/crypt_cache_fl.cpp', 'src/proxy/gz_buffer.cpp',
                       'src/proxy/crypt_cache.cpp', 'src/proxy/loading_trace.cpp', 'src/proxy/resource_reader_core.cpp',
                       'src/engine/fl/resource_reader_fl.cpp'):
            self.assertIn(needle, fl)
        self.assertRegex(cmake, r'set_source_files_properties\(src/engine/fl/crypt_cache_fl\.cpp\n'
                                r'\s*PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-mno-sse;-mno-mmx;-mfpmath=387"\)')

    def test_capture(self):
        capture = strip((ROOT / 'src/proxy/capture.cpp').read_text())
        block = capture[capture.index('#if X3M_EDITION_FL'):]
        gz = capture.index('fl::gz_buffer_fl::initialize();')
        self.assertLess(capture.rindex('music_keep::initialize();'), gz)
        self.assertLess(gz, capture.index('fl::crypt_cache_fl::initialize();'))
        self.assertNotIn('reason=not_ported', capture)
        self.assertIn('fl::crypt_cache_fl::report("session")', capture)
        # the reader and pool claim after the keep and gz_buffer (their close_wrapper window holds 0x10696c)
        self.assertLess(gz, capture.index('resource_reader::initialize();', gz))
        self.assertIn('resource_reader::report();', block)

    def test_loading_trace_fl_branch(self):
        trace = (ROOT / 'src/proxy/loading_trace.cpp').read_text()
        self.assertIn('X3M_LOADING_D3DX "d3dx9_43.dll"', trace)
        self.assertIn('fl_mesh_contract(target, slots)', trace)
        self.assertIn('mesh_adjacency_config', trace)

    def test_schema(self):
        schema = (ROOT / 'tools/config/schema.py').read_text()
        for key in ('crypt_cache', 'gz_buffer', 'resource_read', 'dat_handles', 'mesh_adjacency'):
            self.assertIsNotNone(re.search(r"entry\('%s'," % key, schema), key)
        self.assertEqual(subprocess.run(['python3', str(ROOT / 'tools/config/generate.py'), '--check'],
                                        capture_output=True).returncode, 0)
        ini = (ROOT / 'assets/x3m.ini').read_text()
        # Each of the five loading keys declares both editions; the ini wraps comments, so compare on the unwrapped text.
        loading = ini[ini.index('[loading]'):ini.index('[engine]')]
        flat = re.sub(r'\n;\s*', ' ', loading)
        self.assertEqual(flat.count("X3: Albion Prelude and X3: Farnham's Legacy."), 5)
        self.assertNotIn('reads natively', flat)


if __name__ == '__main__':
    unittest.main()
