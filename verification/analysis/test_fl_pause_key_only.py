"""Host tests of the FL pause key (src/engine/fl/pause_key_only_fl.cpp, pause_key_only_sites_fl.h, inline_patch_fl.h).

Site and ABI: docs/reverse-engineering/fl/pause-background.md, "Port (2026-11-03)" (verifier
verification/probe/verify_pause_key_only_sites_fl.py). (a) The native fixture verification/probe/fl_pause_key_only.cpp
(MinGW i686, run natively through WSL interop) lays the input function's entry and wait-loop windows into a 64 KB-aligned
private image (fixups relocated to that base), runs the real bytes from the function entry with scripted keys, mouse and
joystick words, vanilla and patched: the named scenarios (another key, a held/released/pressed pause key, a click, held
and new mouse bits, joystick rising/held/lost, a focus change, Shift variants, the unpaused path) against the loop's
rule, every key 1..0x1fff, register/locals/DF parity at the same exits, LastError; installs through the production
module (plain write, read back, protection restored), refuses every single-bit window change, every fixup bound to
another base, an already patched site, bad keys and bases; initialize()'s rows (unset, off, default, the rebound
decimal and 0x forms, Shift, eight bad keys, not X3FL.exe, late_claim); shutdown only over its own bytes; the span
sequence's rollback paths. (b) The verifier on the installed X3FL.exe and its refusal of a patched copy. (c) Wiring.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROVIDER = ROOT / 'src/engine/fl/pause_key_only_fl.cpp'
SITES = ROOT / 'src/engine/fl/pause_key_only_sites_fl.h'
FIXTURE = ROOT / 'verification/probe/fl_pause_key_only.cpp'
VERIFIER = ROOT / 'verification/probe/verify_pause_key_only_sites_fl.py'
EXE = Path('/mnt/c/X3FL/X3FL.exe')


def build(tmp):
    """Builds the fixture from the production sources (CMake per-file flags); None when the toolchain or interop is
    absent."""
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
        return None
    tmp = Path(tmp)
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-mstackrealign', '-mincoming-stack-boundary=2',
              '-DX3M_EDITION_FL=1', '-DX3M_PAUSE_KEY_ONLY_FL_FIXTURE', '-msse2', '-mfpmath=sse']
    units = (('src/engine/fl/pause_key_only_fl.cpp', ['-fno-exceptions']), ('src/proxy/engine_patch.cpp', []),
             ('src/engine/fl/executable_identity_fl.cpp', []))
    objects = []
    for source, flags in units:
        obj = tmp / (Path(source).stem + '.o')
        subprocess.run([compiler, *common, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = tmp / 'fl_pause_key_only.exe'
    subprocess.run([compiler, *common, '-static', str(FIXTURE), *objects, '-o', str(exe)], check=True)
    return exe


class FlPauseKeyOnlyNativeFixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='x3-fl-pause-')
        cls.exe = build(cls.tmp.name)
        cls.run_ = subprocess.run([str(cls.exe)], text=True, capture_output=True, timeout=300) if cls.exe else None

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        if self.exe is None:
            self.skipTest('needs i686-w64-mingw32-g++ and WSL interop (a native Windows run)')

    def test_full_run(self):
        run = self.run_
        out = run.stdout
        self.assertEqual(run.returncode, 0, '\n'.join(l for l in out.splitlines() if l.startswith('FAIL'))[:4000]
                         + run.stderr[-1000:])
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0')
        self.assertGreaterEqual(int(re.search(r'RESULT PASS checks=(\d+)', out).group(1)), 90)
        self.assertIn('SWEEP vanilla keys=8191 exits=8190 bad=0', out)
        self.assertIn('SWEEP patched key=0x1b5 keys=8191 exits=1 bad=0', out)
        self.assertIn('SWEEP patched key=0x2c keys=8191 exits=1 bad=0', out)
        self.assertIn('FLIPS windows=2 bits=1568 refused=1568 fixups=6 fixups_refused=6', out)
        self.assertIn('SITE patched=6681feb50175063b7424147545', out)
        rows = re.findall(r'^LOG (pause_key_only .*)$', out, re.M)
        self.assertTrue(rows and all(' edition=fl ' in r and ' rva=0x07880c ' in r for r in rows), rows)
        self.assertEqual(sum('patched=1' in r and 'reason=ok' in r and 'write=plain' in r for r in rows), 5)
        self.assertIn('reason=late_claim', rows[-1])


@unittest.skipUnless(EXE.is_file(), 'X3FL.exe not present')
class FlPauseKeyOnlySites(unittest.TestCase):
    def run_verifier(self, exe):
        return subprocess.run([sys.executable, str(VERIFIER), '--exe', str(exe)], capture_output=True, text=True,
                              timeout=300, cwd=VERIFIER.parent)

    def test_installed_executable(self):
        run = self.run_verifier(EXE)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn('"result": "PASS"', run.stdout)

    def test_patched_copy_fails(self):
        data = bytearray(EXE.read_bytes())
        sys.path.insert(0, str(VERIFIER.parent))
        from verify_chase_camera_site import FlImage
        img = FlImage(bytes(data))
        offset = img.file_offset(img.image_base + 0x07880c)
        data[offset:offset + 13] = bytes.fromhex('6681feb50175063b7424147545')
        with tempfile.TemporaryDirectory() as tmp:
            copy = Path(tmp) / 'X3FL.exe'
            copy.write_bytes(bytes(data))
            run = self.run_verifier(copy)
        self.assertEqual(run.returncode, 1, run.stdout)
        self.assertIn('"windows": false', run.stdout)
        self.assertIn('"site": false', run.stdout)


def strip(text):
    return re.sub(r'//.*', '', text)


class FlPauseKeyOnlyWiring(unittest.TestCase):
    def test_edition_lists_and_capture(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        linked = re.search(r'set\(X3M_AP_ENGINE_LINKED_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        shared = re.search(r'set\(X3M_SHARED_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        self.assertNotIn('pause_key_only.cpp', linked + shared)  # AP only: the fl target links its own provider
        self.assertIn('src/engine/fl/pause_key_only_fl.cpp', cmake.split('target_compile_definitions(d3d9 PRIVATE X3M_EDITION_AP=1)')[1])
        capture = strip((ROOT / 'src/proxy/capture.cpp').read_text())
        self.assertEqual(capture.count('pause_key_only::initialize();'), 2)
        second = capture.rindex('pause_key_only::initialize();')
        block = capture[capture.rindex('#if X3M_EDITION_FL', 0, second):second]
        self.assertNotIn('#endif', block)
        self.assertIn('fov::initialize();', block)

    def test_provider_contract(self):
        code = strip(PROVIDER.read_text())
        for va in ('0x400000', '0x47880c', '0x4787ee', '0x004043a5', '0x5a8478', '0x6243cc'):
            self.assertNotIn(va, code.lower())
        for needle in ('x3m::fl::module_base()', 'engine_patch::install_window_open()', 'P::plan(base, read_window)',
                       'I::install(ops, at, P::original, replacement_, P::site_length', 'core::parse_key(key_text, &key)',
                       'L"X3M_PAUSE_KEY_ONLY"', 'L"X3M_PAUSE_KEY"', 'SetLastError(error);',
                       '"pause_key_only patched=%u key=0x%lx reason=%s requested=%u site=0x%08lx rva=0x%06lx edition=fl '
                       'write=%s"'):
            self.assertIn(needle, code)
        for forbidden in ('float ', 'double ', 'pin_self', 'Emitter'):
            self.assertNotIn(forbidden, code)
        header = strip(SITES.read_text())
        self.assertNotIn('windows.h', header)
        for rva in ('0x078690', '0x0787ee', '0x07880c', '0x078819', '0x07885e', '0x1a8478', '0x2243cc', '0x22ddf0'):
            self.assertIn(rva, header)


if __name__ == '__main__':
    unittest.main()
