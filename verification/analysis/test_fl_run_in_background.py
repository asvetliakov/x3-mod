"""Host tests of the FL run in background (src/engine/fl/run_in_background_fl.cpp, run_in_background_sites_fl.h,
inline_patch_fl.h).

Site and ABI: docs/reverse-engineering/fl/pause-background.md, "Port (2026-11-03)" (verifier
verification/probe/verify_run_in_background_sites_fl.py). (a) The native fixture verification/probe/fl_run_in_background.cpp
(MinGW i686, run natively through WSL interop) lays the startup decision and the message pump's gate into a 64 KB-aligned
private image (fixups relocated to that base) and runs the real bytes: every (seed word, choice, registry value) case
vanilla and patched against the rule, the registry read only for -1 with ECX = the value name, the input setup call's
registers, ESP, return address and DF identical (only the bit and dead arithmetic flags differ), LastError; the pump
gate's blocking / non-blocking rule, the CrossOver shortcut and -noruninbg blocking unfocused before and running after
the patch, a later script clear blocking again; installs through the production module (one lock cmpxchg8b, read back,
protection restored), refuses every single-bit window change, every fixup bound to another base, an already patched
site and bad bases; initialize()'s rows (unset, 0, 1, five invalid values, not X3FL.exe, late_claim); shutdown only over
its own bytes; the span sequence's rollback paths. (b) The verifier on the installed X3FL.exe and its refusal of a
patched copy. (c) Wiring and the schema text.
"""
from pathlib import Path
import importlib.util
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROVIDER = ROOT / 'src/engine/fl/run_in_background_fl.cpp'
SITES = ROOT / 'src/engine/fl/run_in_background_sites_fl.h'
FIXTURE = ROOT / 'verification/probe/fl_run_in_background.cpp'
VERIFIER = ROOT / 'verification/probe/verify_run_in_background_sites_fl.py'
EXE = Path('/mnt/c/X3FL/X3FL.exe')


def build(tmp):
    """Builds the fixture from the production sources (CMake per-file flags); None when the toolchain or interop is
    absent."""
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
        return None
    tmp = Path(tmp)
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-mstackrealign', '-mincoming-stack-boundary=2',
              '-DX3M_EDITION_FL=1', '-DX3M_RUN_IN_BACKGROUND_FL_FIXTURE', '-msse2', '-mfpmath=sse']
    units = (('src/engine/fl/run_in_background_fl.cpp', ['-fno-exceptions']), ('src/proxy/engine_patch.cpp', []),
             ('src/engine/fl/executable_identity_fl.cpp', []))
    objects = []
    for source, flags in units:
        obj = tmp / (Path(source).stem + '.o')
        subprocess.run([compiler, *common, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = tmp / 'fl_run_in_background.exe'
    subprocess.run([compiler, *common, '-static', str(FIXTURE), *objects, '-o', str(exe)], check=True)
    return exe


class FlRunInBackgroundNativeFixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='x3-fl-rib-')
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
        self.assertGreaterEqual(int(re.search(r'RESULT PASS checks=(\d+)', out).group(1)), 30)
        self.assertIn('DECIDE vanilla cases=40 bad=0 bit_set=30', out)
        self.assertIn('DECIDE patched cases=40 bad=0 bit_set=40', out)
        self.assertIn('PARITY cases=40 bad=0 words_changed=10', out)
        self.assertIn('GATE cases=10 bad=0', out)
        self.assertIn('FLIPS windows=2 bits=752 refused=752 fixups=5 fixups_refused=5', out)
        self.assertIn('UNFOCUSED shortcut_word=00005000 noruninbg_word=00005000 path=peek', out)
        rows = re.findall(r'^LOG (run_in_background .*)$', out, re.M)
        self.assertTrue(rows and all(' edition=fl ' in r and ' rva=0x0776ec ' in r and r.endswith(' handler=none')
                                     for r in rows), rows)
        self.assertEqual(sum('status=armed reason=ok' in r and 'write=atomic' in r for r in rows), 1)
        self.assertIn('reason=late_claim', rows[-1])


@unittest.skipUnless(EXE.is_file(), 'X3FL.exe not present')
class FlRunInBackgroundSites(unittest.TestCase):
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
        offset = img.file_offset(img.image_base + 0x0776ec)
        data[offset:offset + 2] = b'\x90\x90'
        with tempfile.TemporaryDirectory() as tmp:
            copy = Path(tmp) / 'X3FL.exe'
            copy.write_bytes(bytes(data))
            run = self.run_verifier(copy)
        self.assertEqual(run.returncode, 1, run.stdout)
        self.assertIn('"windows": false', run.stdout)
        self.assertIn('"site": false', run.stdout)


def strip(text):
    return re.sub(r'//.*', '', text)


class FlRunInBackgroundWiring(unittest.TestCase):
    def test_edition_lists_and_capture(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        linked = re.search(r'set\(X3M_AP_ENGINE_LINKED_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        shared = re.search(r'set\(X3M_SHARED_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        self.assertNotIn('run_in_background.cpp', linked + shared)  # AP only: the fl target links its own provider
        self.assertIn('src/engine/fl/run_in_background_fl.cpp',
                      cmake.split('target_compile_definitions(d3d9 PRIVATE X3M_EDITION_AP=1)')[1])
        capture = strip((ROOT / 'src/proxy/capture.cpp').read_text())
        self.assertEqual(capture.count('run_in_background::initialize();'), 2)
        second = capture.rindex('run_in_background::initialize();')
        block = capture[capture.rindex('#if X3M_EDITION_FL', 0, second):second]
        self.assertNotIn('#endif', block)
        self.assertIn('pause_key_only::initialize();', block)

    def test_provider_contract(self):
        code = strip(PROVIDER.read_text())
        for va in ('0x400000', '0x4776ec', '0x4776c2', '0x004033c9', '0x5a8478', '0x4d2580'):
            self.assertNotIn(va, code.lower())
        for needle in ('x3m::fl::module_base()', 'engine_patch::install_window_open()', 'R::plan(base, read_window)',
                       'I::install(ops, at, R::original, R::patched, R::site_length', 'sites::parse_setting(text, length)',
                       'L"X3M_RUN_IN_BACKGROUND"', 'SetLastError(error);',
                       '"run_in_background site=0x%08lx rva=0x%06lx edition=fl status=%s reason=%s setting=%s '
                       'value_before=- "'):
            self.assertIn(needle, code)
        for forbidden in ('float ', 'double ', 'pin_self', 'claim_call', 'asm(', 'InterlockedOr'):
            self.assertNotIn(forbidden, code)
        header = strip(SITES.read_text())
        self.assertNotIn('windows.h', header)
        for rva in ('0x0776c2', '0x0776ec', '0x0776f6', '0x0ec632', '0x0ec651', '0x0ec6f8', '0x1a8478', '0x1aaf34'):
            self.assertIn(rva, header)

    def test_schema_text(self):
        spec = importlib.util.spec_from_file_location('x3m_schema', ROOT / 'tools/config/schema.py')
        schema = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(schema)
        entries = {e['key']: e for e in schema.SETTINGS}
        for key in ('pause_key_only', 'pause_key', 'run_in_background'):
            self.assertTrue(entries[key]['description'].endswith("X3: Albion Prelude and X3: Farnham's Legacy."), key)


if __name__ == '__main__':
    unittest.main()
