"""Host tests of the FL base ship steering (src/engine/fl/ship_steering_fl.cpp, ship_steering_sites_fl.h, ship_steering_fl_internal.h;
docs/reverse-engineering/fl/ship-steering.md, "Port (2026-11-03)").

(a) The native fixture verification/probe/fl_ship_steering.cpp (MinGW i686 with the project's -mstackrealign
-mincoming-stack-boundary=2 -msse2 -mfpmath=sse, run natively through WSL interop) over the installed X3FL.exe mapped and relocated
into a private image, fed with the ship_trace rows the engine-model harness emits (verification/probe/ship_steering_host.cpp
--emit-trace): window refusal, read-back rollbacks, the A/B differential against the shared core with the AP glue's inputs, the class
filter, the SETA-scaled clamp age, a recycled pointer, the CPU state at every stub path, shutdown and the late window. Skipped
without the compilers, WSL interop or the exe.
(b) The verifier against the installed X3FL.exe (also compares the header's window rows).
(c) Wiring: the FL initializer and frame boundary in capture.cpp, the FL CMake sources, the schema text, no AP address in the FL
module, the shared cores reused.
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
SITES = ROOT / 'src/engine/fl/ship_steering_sites_fl.h'
MODULES = (ROOT / 'src/engine/fl/ship_steering_fl.cpp', ROOT / 'src/engine/fl/ship_avoidance_fl.cpp',
           ROOT / 'src/engine/fl/ship_steering_fl_internal.h')
HOST = ROOT / 'verification/probe/ship_steering_host.cpp'
EXE = Path('/mnt/c/X3FL/X3FL.exe')
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
         '-mincoming-stack-boundary=2', '-msse2', '-mfpmath=sse', '-DX3M_EDITION_FL=1']
sys.path.insert(0, str(ROOT / 'verification/probe'))


def compiler():
    return shutil.which('i686-w64-mingw32-g++')


def strip(text):
    return re.sub(r'//.*', '', text)


def build_fixture(tmp, name):
    """The fixture `name` (the two production units with the fixture define, engine_patch, engine_memory) in `tmp`, or None when
    the toolchain, interop or exe is absent."""
    if not compiler() or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists() or not EXE.is_file():
        return None
    tmp = Path(tmp)
    objects = []
    for obj, source, extra in (
            ('engine_memory.o', ROOT / 'src/proxy/engine_memory.cpp', ['-mno-sse', '-mno-mmx', '-mfpmath=387']),
            ('engine_patch.o', ROOT / 'src/proxy/engine_patch.cpp', []),
            ('ssf.o', MODULES[0], ['-fno-exceptions', '-DX3M_SHIP_STEERING_FL_FIXTURE']),
            ('saf.o', MODULES[1], ['-fno-exceptions', '-DX3M_SHIP_STEERING_FL_FIXTURE']),
            ('fixture.o', ROOT / f'verification/probe/{name}.cpp', ['-DX3M_SHIP_STEERING_FL_FIXTURE'])):
        subprocess.run([compiler(), *FLAGS, *extra, '-c', str(source), '-o', str(tmp / obj)], check=True)
        objects.append(str(tmp / obj))
    exe = tmp / f'{name}.exe'
    subprocess.run([compiler(), *FLAGS, '-static', *objects, '-o', str(exe)], check=True)
    return exe


def windows_path(path):
    return subprocess.run(['wslpath', '-w', str(path)], capture_output=True, text=True, check=True).stdout.strip()


class FlShipSteeringFixture(unittest.TestCase):
    def test_native_fixture(self):
        if not shutil.which('g++'):
            self.skipTest('needs g++ for the engine-model harness')
        with tempfile.TemporaryDirectory(prefix='x3-fl-steer-') as tmp:
            exe = build_fixture(tmp, 'fl_ship_steering')
            if exe is None:
                self.skipTest('needs i686-w64-mingw32-g++, WSL interop (a native Windows run) and the installed X3FL.exe')
            host = Path(tmp) / 'host.out'
            subprocess.run(['g++', '-std=c++17', '-O2', str(HOST), '-o', str(host)], check=True)
            rows = Path(tmp) / 'rows.log'
            emitted = subprocess.run([str(host), '--emit-trace', str(rows)], capture_output=True, text=True, check=True).stdout
            self.assertIn('trace_rows=7093', emitted)
            run = subprocess.run([str(exe), windows_path(EXE), windows_path(rows)], text=True, capture_output=True, timeout=600)
        out = run.stdout
        self.assertEqual(run.returncode, 0, '\n'.join(l for l in out.splitlines() if l.startswith('FAIL'))[:4000])
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0')
        self.assertRegex(out, r'IMAGE base=0x[0-9a-f]{4}0000 size=0x00391000 timestamp=0x618416c8 relocated=38327')
        self.assertIn('FLIPS windows=24 flips=24 refused=24', out)
        self.assertIn('ROLLBACK claims=4 rolled_back=4', out)
        self.assertIn('DIFF a_rows=5689 a_handled=5685 a_passed=4 a_same=5689 ship_same=5689 b_rows=7093 b_clamped=373 b_same=7093', out)
        self.assertIn('CLASSES mask=TS a_rows=5689 excluded=3961 a_handled=1725 a_same=5689 ship_same=5689 b_same=7093', out)
        self.assertRegex(out, r'SETA cases=16 ok=16 clamped_beyond_250ms=[1-9]\d* clamped_beyond_window=0 window_ms\(16\)=250 window_ms\(200\)=800')
        self.assertIn('RECYCLED resets=1', out)
        self.assertIn('CPU runs=32 ok=32', out)
        self.assertRegex(out, r'(?m)^LOG ship_steering installed=1 edition=fl base=0x[0-9a-f]{8} sites=4 class_mask=0x0000 ')
        for group in ('ship_steering_gates', 'ship_steering_gates_capital', 'ship_steering_gates_traffic', 'ship_steering_gates_lane',
                      'ship_steering_dock'):
            self.assertIn(f'LOG {group} installed=0 edition=fl status=ap_only reason=not_ported', out)


class FlShipSteeringSites(unittest.TestCase):
    def test_verifier_against_the_installed_exe(self):
        if not EXE.is_file():
            self.skipTest(f'{EXE} not present')
        try:
            import capstone  # noqa: F401
        except ImportError:
            self.skipTest('needs capstone')
        import verify_ship_steering_sites_fl as probe
        report = probe.verify(EXE.read_bytes())
        self.assertEqual(report['result'], 'PASS', json.dumps({k: v['ok'] for k, v in report['checks'].items()}))
        checks = report['checks']
        self.assertEqual(checks['windows']['count'], 24)
        self.assertEqual(checks['spans']['b_direct_sources'], 18)
        self.assertEqual([l['callers'] for l in checks['leaves']['leaves']], [16, 2, 2])
        self.assertEqual(len(checks['getters']['functions']), 9)
        self.assertEqual(checks['bindings']['large_mask'], '0xe91d')
        self.assertEqual(checks['bindings']['capital_mask'], '0x211d')
        self.assertFalse(any(a['matches'] for a in checks['ap_refused']['ap_spans']))

    def test_header_matches_the_verifier_tables(self):
        import verify_ship_steering_sites_fl as probe
        header = SITES.read_text()
        for name, rva, hexbytes, _ in probe.SPANS:
            raw = bytes.fromhex(hexbytes)
            row = re.search(rf'\{{"{name}", 0x{rva:06x}, \{{([^}}]*)\}}, {len(raw)},', header)
            self.assertTrue(row, name)
            self.assertEqual(row.group(1), ', '.join(f'0x{b:02x}' for b in raw), name)


class FlShipSteeringWiring(unittest.TestCase):
    def test_capture_cmake_schema(self):
        capture = strip((ROOT / 'src/proxy/capture.cpp').read_text())
        fl = capture[capture.index('    music_alert::initialize();\n'):]
        fl = fl[:fl.index('#endif')]
        self.assertIn('ship_steering::initialize();', fl)
        boundary = capture[capture.index('ship_steering::frame_boundary(ctx.frame);') - 400:capture.index('ship_steering::frame_boundary(ctx.frame);')]
        self.assertNotIn('#if !X3M_EDITION_FL', boundary.split('dust_leak_fix::report(ctx.frame);')[-1])
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        fl = cmake[cmake.index('else()\n  add_library(d3d9 SHARED ${X3M_SHARED_SOURCES}'):]
        fl = fl[:fl.index('endif()')]
        self.assertIn('src/engine/fl/ship_steering_fl.cpp src/engine/fl/ship_avoidance_fl.cpp', fl)
        groups = cmake[cmake.index('set(X3M_SHARED_SOURCES'):cmake.index('set(X3M_AP_ENGINE_SOURCES')]
        self.assertNotIn('src/proxy/ship_steering_decide.cpp', groups)  # the AP glue stays AP only
        schema = (ROOT / 'tools/config/schema.py').read_text()
        for key, both in (('ship_steering', True), ('ship_steering_classes', True), ('ship_avoidance', True),
                          ('ship_steering_gates', False), ('ship_steering_gates_capital', False), ('ship_steering_gates_traffic', False),
                          ('ship_steering_gates_lane', False), ('ship_steering_dock', False)):
            entry = schema[schema.index(f"entry('{key}',"):]
            entry = entry[:entry.index('\n    entry(')]
            want = "X3: Albion Prelude and X3: Farnham\\'s Legacy." if both else \
                "X3: Albion Prelude only for now; ignored on Farnham\\'s Legacy."
            self.assertIn(want, entry, key)

    def test_module_binds_fl_only_and_reuses_the_cores(self):
        text = ''.join(strip(p.read_text()) for p in MODULES) + strip(SITES.read_text())
        for ap in ('0x00606fd4', '0x00606f34', '0x0044ccc0', '0x0044c620', '0x0044c970', '0x004593dd', '0x0044dd90', '0x00450980',
                   '0x00450b80', '0x606fd4', '0x606f34', '0x44ccc0', '0x4593dd', '0x44dd90'):
            self.assertNotIn(ap, text, ap)
        for core in ('core::steer(in, dyn, &cmd)', 'core::clamp_for(', 'core::clamp_units(', 'core::servo_tick(', 'av::resolve(',
                     'wk::fill_snapshot(', 'av::site_e_verdict(in)', 'core::parse_class_mask('):
            self.assertIn(core, text, core)
        module = strip(MODULES[0].read_text())
        for envelope in ('pushfd', 'fnsave [esp+0x80]', 'frstor [esp+0x80]', 'stmxcsr [esp+0xec]', 'ldmxcsr [esp+0xec]', 'cld', 'popfd'):
            self.assertIn(envelope, module, envelope)
        self.assertIn('force_align_arg_pointer', module)


if __name__ == '__main__':
    unittest.main()
