"""Host tests of the FL sun light poll (src/engine/fl/sun_light_poll_fl.cpp).

docs/architecture/farnhams-legacy-sun-light.md. (a) The native fixture verification/probe/fl_sun_light_poll.cpp
(MinGW i686, run natively through WSL interop) links the production sun_light_poll_fl.cpp, engine_memory.cpp and
executable_identity_fl.cpp with the CMake per-file flags. Against a synthetic relocated image holding the 50
generated anchors and a fake render context it proves: the gate (admitted at its base, refused for a foreign base,
every single-byte anchor flip, an unreadable slot); the engine rule (a dim directional light over bright point
lights, the range threshold admitting a long-range point light, the truncated double-precision score); the
admission rule's winner when the engine rule admits nothing; the layout refusals (slot count != 8, a non-light
entry, 255 entries without terminator, an unreadable context or node); the 0xc4-byte node read bound; LastError;
the fl.sun_light_sample row; and the per-poll cost. (b) The edition wiring and the provider's contract. X3FL.exe
is not read here (test_fl_sun_light_constants.py owns the fragment).
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROVIDER = ROOT / 'src/engine/fl/sun_light_poll_fl.cpp'
FIXTURE = ROOT / 'verification/probe/fl_sun_light_poll.cpp'


def luma(r, g, b):
    """The engine's score without the directional bonus: trunc((0.587 G + 0.299 R) + 0.114 B), double precision."""
    return int((g * 0.587 + r * 0.299) + b * 0.114)


def build_and_run(tmp):
    """Builds the fixture from the production sources with the CMake per-file flags and runs it natively.
    Returns the CompletedProcess; None when the toolchain or interop is absent."""
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
        return None
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-mstackrealign', '-mincoming-stack-boundary=2',
              '-DX3M_EDITION_FL=1', '-DX3M_FL_SUN_LIGHT_FIXTURE']
    light = ['-mno-sse', '-mno-mmx', '-mfpmath=387']
    sse = ['-msse2', '-mfpmath=sse']
    objects = []
    for source, flags in (('src/engine/fl/sun_light_poll_fl.cpp', ['-fno-exceptions', *sse]),
                          ('src/proxy/engine_memory.cpp', light), ('src/engine/fl/executable_identity_fl.cpp', sse)):
        obj = Path(tmp) / (Path(source).stem + '.o')
        subprocess.run([compiler, *common, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = Path(tmp) / 'fl_sun_light_poll.exe'
    subprocess.run([compiler, *common, *sse, '-static', str(FIXTURE), *objects, '-o', str(exe)], check=True)
    return subprocess.run([str(exe)], text=True, capture_output=True, timeout=120)


class FlSunLightNativeFixture(unittest.TestCase):
    """The production sun_light_poll_fl.cpp against a synthetic image and a fake render context."""

    def test_gate_selection_refusals_bound_row_and_cost(self):
        with tempfile.TemporaryDirectory(prefix='x3-fl-sun-') as tmp:
            result = build_and_run(tmp)
        if result is None:
            self.skipTest('needs i686-w64-mingw32-g++ and WSL interop (a native Windows run)')
        out = result.stdout
        self.assertEqual(result.returncode, 0, out[-4000:] + result.stderr[-1000:])
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0')
        self.assertNotIn('FAIL ', out)
        flips = re.search(r'^ANCHOR flips=(\d+) refused=(\d+)$', out, re.M)
        self.assertIsNotNone(flips)
        self.assertGreater(int(flips.group(1)), 400)
        self.assertEqual(flips.group(1), flips.group(2))
        # The scores the fixture asserts are the engine's truncation, not the AP reader's rounding.
        self.assertIn(f'CASE A status=ok score=0x{0x300 + luma(100, 50, 20):x} candidates=3 slot_admitted=1 admitted=1',
                      out)
        self.assertIn(f'CASE B status=ok score=0x{luma(200, 180, 160):x} slot_admitted=1 admitted=0', out)
        self.assertIn(f'CASE C1 score=0x{0x300 + luma(0, 2, 0):x} second=0x{0x300 + luma(3, 0, 0):x}', out)
        self.assertIn(f'CASE C2 score=0x{0x300 + luma(128, 128, 128):x} second=0x300', out)
        self.assertEqual((luma(3, 0, 0), luma(128, 128, 128)), (0, 127))  # rounded: 1 and 128
        self.assertIn(f'CASE D status=ok engine_rule=0 score=0x{0x300 + luma(40, 40, 40):x} '
                      f'second=0x{0x300 + luma(10, 10, 10):x}', out)
        self.assertIn('CASE none status=no_directional', out)
        for count in (0, 7, 9, 0xffffffff):
            self.assertIn(f'CASE slot_count={count} status=layout', out)
        self.assertIn('CASE non_light status=layout', out)
        self.assertIn('CASE unterminated status=layout', out)
        self.assertIn('CASE context_unreadable status=unreadable', out)
        self.assertIn('NODE_SPAN 0xc4', out)
        self.assertIn('BOUND tested=4 accepted=2 refused=2', out)
        self.assertEqual(len(re.findall(r'^LOG fl\.sun_light_sample ', out, re.M)), 2)  # forced + the timing run's
        self.assertRegex(out, r'TIMING warm16_ns=\d+ cold16_ns=\d+ warm254_ns=\d+ cold254_ns=\d+')


class FlSunLightWiring(unittest.TestCase):
    def test_edition_lists_and_loader(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        block = lambda name: re.search(rf'set\({name}\n(.*?)\)\n', cmake, re.S).group(1)
        self.assertNotIn('sun_light_poll', block('X3M_AP_ENGINE_LINKED_SOURCES'))
        self.assertNotIn('sun_light_poll', block('X3M_SHARED_SOURCES'))
        self.assertIn('src/proxy/sun_light_poll.cpp', block('X3M_AP_SOURCES'))
        fl_branch = cmake[cmake.index('if(X3M_EDITION STREQUAL "ap")'):]
        fl_branch = fl_branch[fl_branch.index('else()'):fl_branch.index('endif()')]
        self.assertIn('target_sources(d3d9 PRIVATE src/engine/fl/sun_light_poll_fl.cpp)', fl_branch)
        self.assertIn('set_source_files_properties(src/engine/fl/sun_light_poll_fl.cpp\n'
                      '  PROPERTIES COMPILE_OPTIONS "-fno-exceptions")', cmake)
        loader = (ROOT / 'src/proxy/loader.cpp').read_text()
        fl_block = loader[loader.index('#if X3M_EDITION_FL\n    // FL edition'):]
        fl_block = fl_block[:fl_block.index('#endif')]
        self.assertIn('x3m::sun_light_poll::initialize();', fl_block)
        schema = (ROOT / 'tools/config/schema.py').read_text()
        self.assertIn("dev('fl_sun_light_log', 'int', 'logging'", schema)
        self.assertIn('"X3M_FL_SUN_LIGHT_LOG", "fl_sun_light_log"', (ROOT / 'src/config/config_schema_inc.h').read_text())

    def test_provider_contract(self):
        code = re.sub(r'//.*', '', PROVIDER.read_text())
        self.assertNotIn('0x400000', code)
        for va in ('0x5aa100', '0x608518', '0x495450'):  # no preferred or AP VAs; RVAs come from the fragment
            self.assertNotIn(va, code.lower())
        self.assertIn('x3m::fl::module_base()', code)
        self.assertIn('x3m::fl::verified()', code)
        self.assertIn('#include "sun_light_poll_fl_inc.h"', code)
        self.assertNotIn('+ 500', code)  # the engine truncates
        self.assertIn('std::int32_t(sum)', code)
        self.assertIn('(double(g) * luma_green + double(r) * luma_red) + double(b) * luma_blue', code)
        for banned in ('new ', 'malloc', 'std::string', 'engine_patch', 'VirtualProtect', 'WriteProcessMemory'):
            self.assertNotIn(banned, code)
        self.assertIn('"fl.sun_light status=%s', code)
        self.assertIn('"fl.sun_light_sample status=%s', code)
        self.assertIn('X3M_FL_SUN_LIGHT_LOG', code)


if __name__ == '__main__':
    unittest.main()
