"""Host tests of the FL SETA manual flight (src/engine/fl/seta_manual_flight_fl.cpp, seta_manual_flight_sites_fl.h;
docs/reverse-engineering/fl/seta.md, "Port (2026-11-03)").

(a) The native fixture verification/probe/fl_seta_manual_flight.cpp (MinGW i686 with the project's -mstackrealign
-mincoming-stack-boundary=2 -msse2 -mfpmath=sse, run natively through WSL interop) over the installed X3FL.exe mapped and
relocated into a private image: install rows, exact-byte refusal of every window, read-back rollbacks (the plain-written
pitch row and strafe Y jump included), part A on the real rows, part B's original and apply paths through the real spans
at every ESP skew, late-window refusal, shutdown restoring every byte. Skipped without the compiler, WSL interop or exe.
(b) The verifier against the installed X3FL.exe (also compares the header's window rows).
(c) Wiring: the FL initializer and present call in capture.cpp, the FL CMake sources, the schema text, no AP address in
the FL module, the shared decision reused.
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
SITES = ROOT / 'src/engine/fl/seta_manual_flight_sites_fl.h'
MODULE = ROOT / 'src/engine/fl/seta_manual_flight_fl.cpp'
FIXTURE = ROOT / 'verification/probe/fl_seta_manual_flight.cpp'
EXE = Path('/mnt/c/X3FL/X3FL.exe')
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
         '-mincoming-stack-boundary=2', '-msse2', '-mfpmath=sse', '-DX3M_EDITION_FL=1']
sys.path.insert(0, str(ROOT / 'verification/probe'))


def compiler():
    return shutil.which('i686-w64-mingw32-g++')


def strip(text):
    return re.sub(r'//.*', '', text)


def run_fixture(tmp):
    """Builds the fixture in `tmp` (the production module with the fixture define, engine_patch, engine_memory) and runs
    it natively on the installed X3FL.exe: the CompletedProcess, or None when the toolchain, interop or exe is absent."""
    if not compiler() or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists() or not EXE.is_file():
        return None
    tmp = Path(tmp)
    objects = []
    for name, source, extra in (
            ('engine_memory.o', ROOT / 'src/proxy/engine_memory.cpp', ['-mno-sse', '-mno-mmx', '-mfpmath=387']),
            ('engine_patch.o', ROOT / 'src/proxy/engine_patch.cpp', []),
            ('smf.o', MODULE, ['-fno-exceptions', '-DX3M_SETA_MANUAL_FLIGHT_FL_FIXTURE']),
            ('fixture.o', FIXTURE, [])):
        subprocess.run([compiler(), *FLAGS, *extra, '-c', str(source), '-o', str(tmp / name)], check=True)
        objects.append(str(tmp / name))
    exe = tmp / 'fl_seta_manual_flight.exe'
    subprocess.run([compiler(), *FLAGS, '-static', *objects, '-o', str(exe)], check=True)
    windows = subprocess.run(['wslpath', '-w', str(EXE)], capture_output=True, text=True, check=True).stdout.strip()
    return subprocess.run([str(exe), windows], text=True, capture_output=True, timeout=300)


class FlSetaManualFlightFixture(unittest.TestCase):
    def test_native_fixture(self):
        with tempfile.TemporaryDirectory(prefix='x3-fl-smf-') as tmp:
            run = run_fixture(tmp)
        if run is None:
            self.skipTest('needs i686-w64-mingw32-g++, WSL interop (a native Windows run) and the installed X3FL.exe')
        out = run.stdout
        self.assertEqual(run.returncode, 0, '\n'.join(l for l in out.splitlines() if l.startswith('FAIL'))[:4000])
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0')
        self.assertGreaterEqual(int(re.search(r'RESULT PASS checks=(\d+)', out).group(1)), 1400)
        self.assertRegex(out, r'IMAGE base=0x[0-9a-f]{4}0000 size=0x00391000 timestamp=0x618416c8 relocated=38327')
        self.assertIn('FLIPS windows=20 flips=37 refused=37', out)
        self.assertIn('CONTRACT original_runs=104 apply_runs=160', out)
        self.assertRegex(out, r'(?m)^LOG seta_manual_flight installed=1 mode=2 edition=fl base=0x[0-9a-f]{8} '
                              r'stop=speed_only stop_sites=8 stop_rows_rva=0x0d217f stop_status=active '
                              r'stop_write=atomic:7,plain:1 stop_plain_rva=0x0d218f yaw=active:atomic '
                              r'pitch=active:atomic roll=active:atomic strafe_y=active:plain strafe_x=active:atomic ')
        self.assertRegex(out, r'(?m)^LOG seta_manual_flight refused reason=late_claim mode=2 edition=fl ')
        self.assertEqual(out.count('LOG seta_manual_flight refused reason=readback_failed mode=2 edition=fl'), 5)
        self.assertEqual(out.count('LOG seta_manual_flight refused reason=patch_rolled_back mode=2 edition=fl'), 4)
        arena = re.search(r'ARENA used=(\d+) capacity=(\d+)', out)
        self.assertTrue(arena and int(arena.group(1)) <= 2048, arena and arena.group(0))


class FlSetaManualFlightSites(unittest.TestCase):
    def test_verifier_against_the_installed_exe(self):
        if not EXE.is_file():
            self.skipTest(f'{EXE} not present')
        try:
            import capstone  # noqa: F401
        except ImportError:
            self.skipTest('needs capstone')
        import verify_seta_manual_flight_sites_fl as probe
        report = probe.verify(EXE.read_bytes())
        self.assertEqual(report['result'], 'PASS', json.dumps({k: v['ok'] for k, v in report['checks'].items()}))
        checks = report['checks']
        self.assertEqual(checks['stop_rows']['straddling'], ['0x0d218f'])
        self.assertEqual(checks['sse_spans']['plain'], ['strafe_y'])
        self.assertEqual(checks['sse_spans']['atomic'], 4)
        self.assertEqual(checks['windows']['count'], 20)
        self.assertFalse(any(a['matches'] for a in checks['ap_refused']['ap_spans']))

    def test_header_matches_the_verifier_tables(self):
        import verify_seta_manual_flight_sites_fl as probe
        header = SITES.read_text()
        for axis, rva, hexbytes, lengths, entry in probe.SSE:
            raw = bytes.fromhex(hexbytes)
            row = re.search(rf'\{{"seta_manual_flight_{axis}_fl", 0x{rva:06x}, \{{([^}}]*)\}}, {len(raw)},', header)
            self.assertTrue(row, axis)
            self.assertEqual(row.group(1), ', '.join(f'0x{b:02x}' for b in raw), axis)
        for name, rva in zip(probe.ROW_NAMES, probe.FLAGS):  # each row's flag is its window's one fixup
            self.assertRegex(header, rf'\{{"stop_{name}", 0x[0-9a-f]{{6}}, 9, 0x[0-9a-f]{{8}}u, 1, \{{\{{2, 0x{rva:06x}\}}\}}, 1,')
        self.assertIn(f'stop_rows_rva = 0x{probe.STOP_ROWS:06x}', header)


class FlSetaManualFlightWiring(unittest.TestCase):
    def test_capture_cmake_schema(self):
        loader = strip((ROOT / 'src/proxy/loader.cpp').read_text())
        fl = loader[loader.index('if (x3m::fl::verified())'):]
        fl = fl[:fl.index('#endif')]
        self.assertLess(fl.index('x3m::chase_transition::initialize();'), fl.index('x3m::seta_restore::initialize();'))
        self.assertLess(fl.index('x3m::seta_restore::initialize();'), fl.index('x3m::seta_manual_flight::initialize();'))
        capture = strip((ROOT / 'src/proxy/capture.cpp').read_text())
        present = re.search(r'#else\n    cull_small_parts::present\(ctx\.id, ctx\.frame, ctx\.capture\); *\n(.*?)#endif',
                            capture, re.S)
        self.assertTrue(present)
        self.assertIn('seta_restore::present(ctx.frame);', present.group(1))
        self.assertIn('seta_manual_flight::present(ctx.frame);', present.group(1))
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        fl = cmake[cmake.index('else()\n  add_library(d3d9 SHARED ${X3M_SHARED_SOURCES}'):]
        fl = fl[:fl.index('endif()')]
        self.assertIn('src/engine/fl/seta_manual_flight_fl.cpp', fl)
        ap_fl_groups = cmake[cmake.index('set(X3M_SHARED_SOURCES'):cmake.index('set(X3M_AP_ENGINE_SOURCES')]
        self.assertNotIn('src/proxy/seta_manual_flight.cpp', ap_fl_groups)  # AP only
        schema = (ROOT / 'tools/config/schema.py').read_text()
        entry = schema[schema.index("entry('seta_manual_flight',"):]
        entry = entry[:entry.index('\n    entry(')]
        self.assertNotIn('Albion Prelude only', entry)  # both editions: no edition note (convention 2026-11-04)

    def test_module_binds_fl_only_and_reuses_the_decision(self):
        module = strip(MODULE.read_text())
        sites = strip(SITES.read_text())
        for ap in ('0x00606f34', '0x0040ebde', '0x00411e0c', '0x004120c5', '0x0041210d', '0x00608e40', '0x00410100',
                   '0x0049f4c0', '0x606f34', '0x40ebde'):
            self.assertNotIn(ap, module + sites, ap)
        self.assertIn('core::decide(axis, in, out)', sites)  # the AP policy, not a copy
        self.assertNotIn('inline Skip decide', sites)
        self.assertIn('fnsave [esp+0x80]', module)
        self.assertIn('frstor [esp+0x80]', module)
        self.assertIn('force_align_arg_pointer', module)
        # the apply path writes no GPR and no flag before POPFD: only SSE instructions between the decision and LEAVE
        for macro in ('X3M_FL_SMF_ROTATION', 'X3M_FL_SMF_STRAFE'):
            body = module[module.index(f'.macro {macro}'):]
            body = body[body.index('je 1f') + 5:body.index('X3M_FL_SMF_LEAVE _x3m_fl_smf_resume')]
            for line in filter(None, (l.strip() for l in body.splitlines())):
                self.assertRegex(line, r'^(mulsd|mulss|cvtpd2ps|cvtps2pd|movd|cvtdq2ps) (xmm|\\out)', (macro, line))


if __name__ == '__main__':
    unittest.main()
