"""Host tests of the FL fog transit prefill walk (src/engine/fl/fog_prefill_fl.cpp).

docs/architecture/farnhams-legacy-prefill.md. (a) The native fixture verification/probe/fl_fog_prefill.cpp (MinGW
i686, run natively through WSL interop) links the production provider, sector_background_fl.cpp, engine_memory.cpp
and executable_identity_fl.cpp and drives the shared fog_prefill::walk() over a synthetic image at a VirtualAlloc
base and a fake manager list (old sector, new sector, ship, cut-scene space, dead and partial sectors): the identity
refusal, an image relocated for another base, every single-byte anchor flip, admission at the image's own base,
Found with the record of the newest class-1/subtype-0 alive node, the recovery guard, NoScene for an unset scene,
SameId/Dead/subtype/class filters, the node bound, Head/NoManager/Malformed/ReadFailure, the record refusals, the
fl.prefill rows, LastError and the per-poll cost. (b) Edition wiring and the provider contract: the AP walk keeps
its literal addresses (ApLayout, no guard read), the FL file reads only module_base() + generated RVAs. X3FL.exe is
not read here (test_fl_fog_prefill_constants.py owns the generated fragment).
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROVIDER = ROOT / 'src/engine/fl/fog_prefill_fl.cpp'
SHARED = ROOT / 'src/proxy/fog_prefill.h'
FIXTURE = ROOT / 'verification/probe/fl_fog_prefill.cpp'


def build_and_run(tmp):
    """Builds the fixture from the production sources with the CMake per-file flags and runs it natively. Returns
    the CompletedProcess; None when the toolchain or interop is absent. Also used by
    verification/results/fl-port/fog-prefill/run_fixture.py."""
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
        return None
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-mstackrealign', '-mincoming-stack-boundary=2',
              '-DX3M_EDITION_FL=1', '-DX3M_FOG_PREFILL_FIXTURE']
    light = ['-mno-sse', '-mno-mmx', '-mfpmath=387']
    sse = ['-msse2', '-mfpmath=sse']
    objects = []
    for source, flags in (('src/engine/fl/fog_prefill_fl.cpp', ['-fno-exceptions', *sse]),
                          ('src/engine/fl/sector_background_fl.cpp', ['-fno-exceptions', *sse]),
                          ('src/proxy/engine_memory.cpp', light), ('src/engine/fl/executable_identity_fl.cpp', sse)):
        obj = Path(tmp) / (Path(source).stem + '.o')
        subprocess.run([compiler, *common, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = Path(tmp) / 'fl_fog_prefill.exe'
    subprocess.run([compiler, *common, *sse, '-static', str(FIXTURE), *objects, '-o', str(exe)], check=True)
    return subprocess.run([str(exe)], text=True, capture_output=True, timeout=120)


class FlPrefillNativeFixture(unittest.TestCase):
    """The production fog_prefill_fl.cpp and its dependencies, linked into the fixture and run natively."""

    def test_admission_walk_guard_filters_refusals_rows_and_cost(self):
        with tempfile.TemporaryDirectory(prefix='x3-fl-prefill-') as tmp:
            result = build_and_run(tmp)
        if result is None:
            self.skipTest('needs i686-w64-mingw32-g++ and WSL interop (a native Windows run)')
        out = result.stdout
        self.assertEqual(result.returncode, 0, out[-4000:] + result.stderr[-1000:])
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0')
        self.assertNotIn('FAIL ', out)
        self.assertIn('ANCHOR anchors=18 bytes=617 flips=617 refused=617', out)
        self.assertIn('FOUND node=new steps=3 reads=36 id=5021 index=14 name=foggreenoutlands_dust near=1500000 '
                      'far=18500000', out)
        self.assertRegex(out, r'TIMING found_ns=[\d.]+ guarded_ns=[\d.]+ walks=100000')
        rows = re.findall(r'^LOG (fl\.prefill\S*) (?:status|row)=(\S+)', out, re.M)
        self.assertIn(('fl.prefill', 'executable_mismatch'), rows)
        self.assertIn(('fl.prefill', 'anchor_mismatch'), rows)
        self.assertIn(('fl.prefill', 'base_overflow'), rows)
        self.assertIn(('fl.prefill', 'ready'), rows)
        self.assertEqual(len(re.findall(r'^LOG fl\.prefill_sample ', out, re.M)), 2)


class FlPrefillWiring(unittest.TestCase):
    def test_edition_lists_capture_fence_and_schema(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        ap = re.search(r'set\(X3M_AP_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        self.assertNotIn('fog_prefill_fl', ap)
        fl_branch = cmake[cmake.index('add_library(d3d9 SHARED ${X3M_SHARED_SOURCES}'):]
        fl_branch = fl_branch[:fl_branch.index('endif()')]
        self.assertIn('target_sources(d3d9 PRIVATE src/engine/fl/fog_prefill_fl.cpp)', fl_branch)
        self.assertIn('set_source_files_properties(src/engine/fl/fog_prefill_fl.cpp PROPERTIES COMPILE_OPTIONS '
                      '"-fno-exceptions")', cmake)
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        poll = capture[capture.index('void fog_prefill_poll(Device& ctx) {'):capture.index('void object_context(')]
        fl = poll[poll.index('#if X3M_EDITION_FL'):poll.index('#else')]
        ap = poll[poll.index('#else'):poll.index('#endif')]
        self.assertIn('if (!fl::fog_prefill_fl::walk(result, ctx.fog_ready_sector_id)) return;', fl)
        # The AP path is the pre-FL code: the identity gate and the 0x400000-based walk.
        self.assertIn('if (!object_trace::executable_verified()) return;', ap)
        self.assertIn('const fog_prefill::Result result = fog_prefill::walk(read, ctx.fog_ready_sector_id);', ap)
        self.assertLess(poll.index('GetLastError()'), poll.index('#if X3M_EDITION_FL'))
        self.assertEqual(fl.count('{'), fl.count('}'))
        self.assertIn('#include "../engine/fl/fog_prefill_fl.h"', capture)
        schema = (ROOT / 'tools/config/schema.py').read_text()
        self.assertIn("dev('fl_prefill_log', 'int', 'logging'", schema)
        self.assertIn('{"X3M_FL_PREFILL_LOG", "fl_prefill_log"', (ROOT / 'src/config/config_schema_inc.h').read_text())

    def test_shared_walk_layouts(self):
        code = re.sub(r'//.*', '', SHARED.read_text())
        ap = re.search(r'struct ApLayout \{(.*?)\};', code, re.S).group(1)
        for value in ('manager = 0x0060850c', 'count = 0x607040', 'table = 0x606fc0', 'guarded = false',
                      'using Record = sector_background::ApLayout;'):
            self.assertIn(value, ap)
        outside = code.replace(ap, '')
        for va in ('0x0060850c', '0x60850c', '0x607040', '0x606fc0'):
            self.assertNotIn(va, outside, va)
        self.assertIn('template <class Read, class Layout = ApLayout> Result walk(Read& read, std::uint32_t '
                      'last_ready_id)', code)
        # The guard read is compiled only for a guarded layout and comes before the manager read.
        walk = code[code.index('Result walk(Read& read'):]
        self.assertLess(walk.index('if constexpr (Layout::guarded)'), walk.index('Layout::manager'))
        self.assertIn('sb::read_record<decltype(counted), typename Layout::Record>(counted, s)', walk)
        # The AP name table is unchanged: "guarded" exists only in the FL edition.
        names = code[code.index('inline const char* name(Walk w)'):]
        names = names[:names.index('return "invalid";')]
        self.assertIn('#if X3M_EDITION_FL\n    case Walk::Guarded: return "guarded";\n#else\n    default: break;', names)

    def test_provider_contract(self):
        code = re.sub(r'//.*', '', PROVIDER.read_text())
        self.assertNotIn('0x400000', code)
        for va in ('0x5a82ec', '0x5a8338', '0x5a83d0', '0x62de10', '0x60850c', '0x607040', '0x606fc0'):
            self.assertNotIn(va, code.lower())
        self.assertIn('x3m::fl::module_base()', code)
        self.assertIn('x3m::fl::verified()', code)
        self.assertIn('sbf::admitted()', code)
        self.assertIn('#include "fog_prefill_fl_inc.h"', code)
        self.assertIn('static constexpr bool guarded = true;', code)
        self.assertIn('static constexpr bool name_to_nul = true;', code)
        self.assertIn('fp::walk<decltype(reader), FlLayout>(reader, last_ready_id)', code)
        self.assertIn('x3m::engine_memory::read', code)
        for status in ('"undecided"', '"ready"', '"executable_mismatch"', '"sector_refused"', '"anchor_mismatch"',
                       '"base_overflow"'):
            self.assertIn(status, code)
        self.assertIn('"fl.prefill status=%s', code)
        self.assertIn('"fl.prefill_sample row=%lu', code)
        self.assertIn('L"X3M_FL_PREFILL_LOG"', code)
        self.assertEqual(code.count('GetLastError()'), code.count('SetLastError(error)'))
        for banned in ('new ', 'malloc', 'std::string', 'float', 'double', 'throw', 'VirtualProtect', 'WriteProcessMemory'):
            self.assertNotIn(banned, code)


if __name__ == '__main__':
    unittest.main()
