"""Host tests of the FL sector-background reader (src/engine/fl/sector_background_fl.cpp).

docs/architecture/farnhams-legacy-sector.md. (a) The native fixture verification/probe/fl_sector_background.cpp
(MinGW i686, run natively through WSL interop) links the production provider, engine_memory.cpp and
executable_identity_fl.cpp and drives the shared sector_background::sample() over a synthetic image at a
VirtualAlloc base: the identity refusal of a foreign executable, an image relocated for another base, every
single-byte anchor flip, admission at the image's own base, Ready with the record fields, the +0x758 view
distance, a 21-character name ending at an uncommitted page (no over-read), the docked parent walk,
NoCockpit/Missing/NoSector/BadClass/BadIndex/BadCount/Loading/BadDust/CameraMismatch/Cycle, the fl.sector rows,
LastError and the per-sample cost. (b) Edition wiring and the provider contract: the AP reader keeps its literal
addresses (ApLayout), the FL file reads only module_base() + generated RVAs. X3FL.exe is not read here
(test_fl_sector_background_constants.py owns the generated fragment).
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROVIDER = ROOT / 'src/engine/fl/sector_background_fl.cpp'
SHARED = ROOT / 'src/proxy/sector_background.h'
FIXTURE = ROOT / 'verification/probe/fl_sector_background.cpp'


def build_and_run(tmp):
    """Builds the fixture from the production sources with the CMake per-file flags and runs it natively. Returns
    the CompletedProcess; None when the toolchain or interop is absent. Also used by
    verification/results/fl-port/sector-background/run_fixture.py."""
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
        return None
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-mstackrealign', '-mincoming-stack-boundary=2',
              '-DX3M_EDITION_FL=1', '-DX3M_SECTOR_BACKGROUND_FIXTURE']
    light = ['-mno-sse', '-mno-mmx', '-mfpmath=387']
    sse = ['-msse2', '-mfpmath=sse']
    objects = []
    for source, flags in (('src/engine/fl/sector_background_fl.cpp', ['-fno-exceptions', *sse]),
                          ('src/proxy/engine_memory.cpp', light), ('src/engine/fl/executable_identity_fl.cpp', sse)):
        obj = Path(tmp) / (Path(source).stem + '.o')
        subprocess.run([compiler, *common, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = Path(tmp) / 'fl_sector_background.exe'
    subprocess.run([compiler, *common, *sse, '-static', str(FIXTURE), *objects, '-o', str(exe)], check=True)
    return subprocess.run([str(exe)], text=True, capture_output=True, timeout=120)


class FlSectorNativeFixture(unittest.TestCase):
    """The production sector_background_fl.cpp, engine_memory.cpp and executable_identity_fl.cpp, linked into the
    fixture and run natively (WSL interop)."""

    def test_admission_record_name_walk_refusals_rows_and_cost(self):
        with tempfile.TemporaryDirectory(prefix='x3-fl-sector-') as tmp:
            result = build_and_run(tmp)
        if result is None:
            self.skipTest('needs i686-w64-mingw32-g++ and WSL interop (a native Windows run)')
        out = result.stdout
        self.assertEqual(result.returncode, 0, out[-4000:] + result.stderr[-1000:])
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0')
        self.assertNotIn('FAIL ', out)
        self.assertIn('ANCHOR anchors=22 bytes=1112 flips=1112 refused=1112', out)
        self.assertIn('READY name=foggreenoutlands_dust dust=50 near=1500000 far=18500000 stardust=7 config758=3 '
                      'effective_far=500000000', out)
        self.assertRegex(out, r'TIMING sample_ns=[\d.]+ no_name_ns=[\d.]+ samples=100000')
        rows = re.findall(r'^LOG (fl\.sector\S*) status=(\S+)', out, re.M)
        self.assertIn(('fl.sector', 'executable_mismatch'), rows)
        self.assertIn(('fl.sector', 'anchor_mismatch'), rows)
        self.assertIn(('fl.sector', 'ready'), rows)
        self.assertEqual(len(re.findall(r'^LOG fl\.sector_sample ', out, re.M)), 1)


class FlSectorWiring(unittest.TestCase):
    def test_edition_lists_and_capture_fence(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        ap = re.search(r'set\(X3M_AP_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        self.assertNotIn('sector_background_fl', ap)
        fl_branch = cmake[cmake.index('add_library(d3d9 SHARED ${X3M_SHARED_SOURCES}'):]
        fl_branch = fl_branch[:fl_branch.index('endif()')]
        self.assertIn('target_sources(d3d9 PRIVATE src/engine/fl/sector_background_fl.cpp)', fl_branch)
        self.assertIn('set_source_files_properties(src/engine/fl/sector_background_fl.cpp PROPERTIES COMPILE_OPTIONS '
                      '"-fno-exceptions")', cmake)
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        body = capture[capture.index('void sector_background_context('):]
        body = body[:body.index('\n}\n')]
        fl = body[body.index('#if X3M_EDITION_FL'):body.index('#else')]
        gate = body[body.index('#else'):body.index('#endif')]
        ap = body[body.index('#if !X3M_EDITION_FL'):]
        ap = ap[:ap.index('#endif')]
        self.assertIn('const bool admitted =\n        fl::sector_background_fl::sample(value, volumetric_fog_docked ? '
                      'sector_background::anchor_walk_limit : 0u);', fl)
        # The AP path is the pre-FL code: the identity gate and the 0x400000-based reader. No fenced line opens a
        # brace (test_sector_background.py extracts the function by brace count).
        self.assertIn('const bool admitted = object_trace::executable_verified();', gate)
        self.assertIn('value = sector_background::sample(read, volumetric_fog_docked ? '
                      'sector_background::anchor_walk_limit : 0u);', ap)
        self.assertEqual(fl.count('{'), fl.count('}'))
        self.assertEqual(gate.count('{'), gate.count('}'))
        self.assertIn('#include "../engine/fl/sector_background_fl.h"', capture)
        schema = (ROOT / 'tools/config/schema.py').read_text()
        self.assertIn("dev('fl_sector_log', 'int', 'logging'", schema)
        self.assertIn('{"X3M_FL_SECTOR_LOG", "fl_sector_log"', (ROOT / 'src/config/config_schema_inc.h').read_text())

    def test_shared_reader_layouts(self):
        code = re.sub(r'//.*', '', SHARED.read_text())
        ap = re.search(r'struct ApLayout \{(.*?)\};', code, re.S).group(1)
        for value in ('registry = 0x608504', 'table = 0x606fc0', 'count = 0x607040', 'config = 0x606f34',
                      'view_distance = 0x768', 'far_floor_high = 500000000', 'far_floor_mid = 100000000',
                      'name_to_nul = false'):
            self.assertIn(value, ap)
        outside = code.replace(ap, '')
        for va in ('0x608504', '0x606fc0', '0x607040', '0x606f34', '0x768'):
            self.assertNotIn(va, outside, va)
        self.assertIn('template <class Read, class Layout = ApLayout> Sample sample(Read& read, unsigned walk = 0)', code)
        self.assertIn('Layout::name_to_nul ? copy_name(read, out.name_pointer, raw) : '
                      'span(read, out.name_pointer, 0, raw, sizeof raw)', code)

    def test_provider_contract(self):
        code = re.sub(r'//.*', '', PROVIDER.read_text())
        self.assertNotIn('0x400000', code)
        for va in ('0x5a82e4', '0x5a83d0', '0x5a8338', '0x5a83b0', '0x608504', '0x606fc0', '0x607040', '0x606f34'):
            self.assertNotIn(va, code.lower())
        self.assertIn('x3m::fl::module_base()', code)
        self.assertIn('x3m::fl::verified()', code)
        self.assertIn('#include "sector_background_fl_inc.h"', code)
        self.assertIn('static constexpr std::uint32_t view_distance = config_view_distance;', code)
        self.assertIn('static constexpr bool name_to_nul = true;', code)
        self.assertIn('sb::sample<decltype(reader), FlLayout>(reader, walk)', code)
        self.assertIn('x3m::engine_memory::read', code)
        for status in ('"undecided"', '"ready"', '"executable_mismatch"', '"anchor_mismatch"', '"base_overflow"'):
            self.assertIn(status, code)
        self.assertIn('"fl.sector status=%s', code)
        self.assertIn('"fl.sector_sample row=%lu', code)
        self.assertIn('L"X3M_FL_SECTOR_LOG"', code)
        self.assertEqual(code.count('GetLastError()'), code.count('SetLastError(error)'))
        for banned in ('new ', 'malloc', 'std::string', 'float', 'double', 'throw', 'VirtualProtect', 'WriteProcessMemory'):
            self.assertNotIn(banned, code)


if __name__ == '__main__':
    unittest.main()
