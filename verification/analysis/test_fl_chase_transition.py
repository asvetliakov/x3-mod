"""Host tests of the FL chase view restore (src/proxy/chase_transition.cpp with X3M_EDITION_FL=1,
src/engine/fl/chase_transition_sites_fl.h; docs/reverse-engineering/fl/chase-transition.md, "Verified 2026-11-02").

(a) The native fixture verification/probe/fl_chase_transition.cpp (MinGW i686, run natively through WSL interop)
includes the production module built for FL, lays the 26 identity windows into a 64 KB-aligned private image, installs
the twelve spans through engine_patch::claim and drives every thunk against a reference copy of the original span (all
GPRs, ESP, EFLAGS with DF, XMM0-7, the x87 control word and stack, MXCSR, LastError and the stack words the span writes,
for every 4-byte ESP skew), the inject thunk also with an engine call inside it; the restore through the thunks (gate
transit, dock, undock, jump drive, retries exhausted, refused call, six cancels), the lifetime witnesses, every window
flip refused before any write, read-back faults rolled back group by group and the late-window refusal.
(b) The AP host harness of the restore core (verification/probe/chase_transition_restore_host.cpp, unchanged on disk)
compiled for FL: the same 28 cases with the FL caller constants and callback selector 32.
(c) The verifier against the installed X3FL.exe (skipped without it) and the header's constants against the verifier.
(d) Edition wiring: loader, capture, chase camera observer, schema text.
"""
from pathlib import Path
import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SITES = ROOT / 'src/engine/fl/chase_transition_sites_fl.h'
FIXTURE = ROOT / 'verification/probe/fl_chase_transition.cpp'
VERIFIER = ROOT / 'verification/probe/verify_chase_transition_sites_fl.py'
COMMON = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
          '-mincoming-stack-boundary=2', '-DX3M_EDITION_FL=1', '-DX3M_CHASE_TRANSITION_FL_FIXTURE', '-msse2',
          '-mfpmath=sse']
sys.path.insert(0, str(ROOT / 'verification/probe'))


def compiler():
    return shutil.which('i686-w64-mingw32-g++')


def strip(text):
    return re.sub(r'//.*', '', text)


class FlChaseTransitionNativeFixture(unittest.TestCase):
    def test_thunks_restore_rollback_and_late_window(self):
        if not compiler() or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
            self.skipTest('needs i686-w64-mingw32-g++ and WSL interop (a native Windows run)')
        with tempfile.TemporaryDirectory(prefix='x3-fl-ct-') as tmp:
            tmp = Path(tmp)
            memory = tmp / 'engine_memory.o'
            subprocess.run([compiler(), *COMMON, '-mno-sse', '-mno-mmx', '-mfpmath=387', '-c',
                            str(ROOT / 'src/proxy/engine_memory.cpp'), '-o', str(memory)], check=True)
            exe = tmp / 'fl_chase_transition.exe'
            # CMake's per-file flag for chase_transition.cpp (-fno-exceptions); engine_patch.cpp is included too.
            subprocess.run([compiler(), *COMMON, '-fno-exceptions', '-static', str(FIXTURE), str(memory), '-o',
                            str(exe)], check=True)
            run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=300)
        out = run.stdout
        self.assertEqual(run.returncode, 0, '\n'.join(l for l in out.splitlines() if l.startswith('FAIL'))[:4000]
                         + run.stderr[-1000:])
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0')
        self.assertGreaterEqual(int(re.search(r'RESULT PASS checks=(\d+)', out).group(1)), 400)
        self.assertIn('WINDOWS count=26 base_aligned=1', out)
        self.assertIn('ATOMIC cmpxchg8b=9 plain=3', out)
        self.assertIn('FLIPS windows=52 refused=52', out)
        self.assertIn('CONTRACT runs=96', out)
        arena = re.search(r'ARENA used=(\d+) capacity=(\d+)', out)
        self.assertTrue(arena and int(arena.group(1)) <= 2048, arena and arena.group(0))
        for name, result, hud in (('gate', 'restored', 'ok'), ('dock', 'restored', 'skipped'),
                                  ('jump', 'restored', 'ok'), ('retries', 'no_effect', 'none')):
            row = re.search(rf'(?m)^SCENARIO {name} .*$', out)
            self.assertTrue(row, name)
            self.assertIn(f'result={result} ', row.group(0))
            self.assertIn(f'hud_refresh={hud} ', row.group(0))
        self.assertRegex(out, r'(?m)^LOG chase_transition installed=1 status=active edition=fl base=0x[0-9a-f]{4}0000 '
                              r'window=- window_closed_by=- diagnostics=0 diagnostics_reason=ap_only observers=1 ')
        self.assertRegex(out, r'(?m)^LOG chase_view_restore installed=1 requested=1 status=ok edition=fl path=engine '
                              r'sites=1 cancel_sites=3 inject_site=0x[0-9a-f]{8} inject_rva=0x0cfd00 call=0x[0-9a-f]{8} '
                              r'call_rva=0x0bb5b0 callback=32 method=SelectMode mode=258 ')
        self.assertRegex(out, r'(?m)^LOG chase_transition installed=0 status=late_claim edition=fl .*'
                              r'window_closed_by=fixture_present ')


class FlRestoreCoreHarness(unittest.TestCase):
    def test_ap_cases_with_fl_constants(self):
        cxx = shutil.which('c++') or shutil.which('g++')
        if not cxx:
            self.skipTest('needs a host C++ compiler')
        source = (ROOT / 'verification/probe/chase_transition_restore_host.cpp').read_text()
        source = source.replace('#include "../../src/proxy/', '#include "')
        # The two argument-0 checks pin AP's selector 0; FL passes the callback-free selector 32.
        fl, n = re.subn(r'\b([ab])\.callback == 0\b', r'\1.callback == 32', source)
        self.assertEqual(n, 2)
        # Case 22 pins AP's load-chain constructor return 0x0041f840; FL's is 0x00420e12 (verified call 0x420e0d).
        fl, n = re.subn(r'on_complete\(0x0041f840u\)', 'on_complete(0x00420e12u)', fl)
        self.assertEqual(n, 1)
        with tempfile.TemporaryDirectory(prefix='x3-fl-restore-') as tmp:
            src = Path(tmp) / 'restore_host_fl.cpp'
            src.write_text(fl)
            exe = Path(tmp) / 'restore_host_fl'
            subprocess.run([cxx, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-DX3M_EDITION_FL=1', '-I',
                            str(ROOT / 'src/proxy'), str(src), '-o', str(exe)], check=True)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stdout[-2000:] + run.stderr[-2000:])
        self.assertIn('checks PASS', run.stdout)


class FlChaseTransitionSites(unittest.TestCase):
    def test_verifier_against_the_installed_exe(self):
        import verify_chase_transition_sites_fl as probe
        exe = probe.FL_DEFAULT_EXE
        if not exe.is_file():
            self.skipTest(f'{exe} not present')
        try:
            import capstone  # noqa: F401
        except ImportError:
            self.skipTest('needs capstone')
        report = probe.verify(exe.read_bytes())
        self.assertEqual(report['result'], 'PASS', json.dumps({k: v['ok'] for k, v in report['checks'].items()}))
        windows = report['checks']['windows']
        header = SITES.read_text()
        for name, rva, length in probe.WINDOW_SPANS:
            w = windows[name]
            fixups = ', '.join(f'{{{o}, 0x{int(t, 16):06x}}}' for o, t in w['fixups'])
            self.assertIn(f'{{0x{rva:06x}, {length}, 0x{w["fnv1a32"]}u, {len(w["fixups"])}, {{{fixups}}},', header,
                          name)

    def test_header_spans_match_the_verifier(self):
        import verify_chase_transition_sites_fl as probe
        header = SITES.read_text()
        for group, kind, name, rva, hexbytes, lengths, relocs in probe.SITES:
            raw = bytes.fromhex(hexbytes)
            for off in relocs:
                raw = raw[:off] + b'\0\0\0\0' + raw[off + 4:]
            row = re.search(rf'\{{"{name}", 0x{rva:06x}, {len(raw)}, \{{([^}}]*)\}}', header)
            self.assertTrue(row, name)
            self.assertEqual(row.group(1), ', '.join(f'0x{b:02x}' for b in raw), name)
        for const, value in (('alloc_return_va', probe.ALLOC_RETURN), ('load_return_va', probe.LOAD_RETURN),
                             ('free_return_va', probe.FREE_RETURN)):
            self.assertIn(f'{const} = 0x{0x400000 + value:08x}', header)
        self.assertIn(f'native_return_rva = 0x{probe.NATIVE_RETURN:06x}', header)
        core = (ROOT / 'src/proxy/chase_transition_restore_core.h').read_text()
        fl = core[core.index('#if X3M_EDITION_FL'):core.index('#else', core.index('#if X3M_EDITION_FL'))]
        self.assertIn(f'restore_free_caller = 0x{0x400000 + probe.FREE_RETURN:06x}', fl)
        self.assertIn(f'restore_alloc_caller = 0x{0x400000 + probe.ALLOC_RETURN:06x}', fl)
        self.assertIn(f'restore_load_constructor_caller = 0x{0x400000 + probe.LOAD_RETURN:06x}', fl)
        self.assertIn('restore_callback_none = 32', fl)


class FlChaseTransitionWiring(unittest.TestCase):
    def test_loader_capture_camera_and_schema(self):
        loader = strip((ROOT / 'src/proxy/loader.cpp').read_text())
        fl = loader[loader.index('if (x3m::fl::verified())'):]
        fl = fl[:fl.index('#endif')]
        self.assertIn('if (x3m::chase_camera::initialize()) x3m::chase_transition::initialize();', fl)
        self.assertNotIn('"chase_transition"', fl)
        capture = strip((ROOT / 'src/proxy/capture.cpp').read_text())
        self.assertRegex(capture, r'#else\n    if \(ctx\.capture \|\| ctx\.frame % 300 == 0\) \{\n'
                                  r'        chase_camera::report\(ctx\.frame\); *\n'
                                  r'        chase_lead::report\(ctx\.frame\); *\n'
                                  r'        chase_transition::report\(ctx\.frame\); *\n'
                                  r'(        chase_fire::report\(ctx\.frame\); *\n)?    \}\n#endif')
        camera = (ROOT / 'src/proxy/chase_camera.cpp').read_text()
        observer = camera[camera.index('chase_transition::AppliedFrame observed;') - 400:
                          camera.index('chase_transition::restore_observe_frame(observed);')]
        self.assertNotIn('#if', observer)  # both editions
        schema = (ROOT / 'tools/config/schema.py').read_text()
        for key in ('chase_view_restore', 'chase_view_restore_retries'):
            entry = schema[schema.index(f"entry('{key}',"):]
            entry = entry[:entry.index('\n    entry(')]
            self.assertNotIn('Albion Prelude only', entry, key)

    def test_fl_branch_has_no_ap_address(self):
        source = (ROOT / 'src/proxy/chase_transition.cpp').read_text()
        blocks = re.findall(r'#if X3M_EDITION_FL\n(.*?)\n#(?:else|endif)', source, re.S)
        self.assertGreaterEqual(len(blocks), 8)
        fl = strip('\n'.join(blocks))
        for ap in ('0x4a3909', '0x41f8d0', '0x410100', '0x49f570', '0x57fc60', '0x608504', '0x60850c'):
            self.assertNotIn(ap, fl)
        self.assertIn('S::preferred(caller, fl_base)', fl)
        self.assertIn('controlled = regs[6]', fl)


if __name__ == '__main__':
    unittest.main()
