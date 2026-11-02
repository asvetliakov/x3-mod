"""Host tests of the FL material-strength texture hook (src/proxy/d3dx_texture_hook.cpp; docs/architecture/
overlay-material-strength.md, "Farnham's Legacy"; ledger docs/verification/farnhams-legacy.md, "Material-strength
texture hook on FL, host evidence").

(a) The native fixture verification/probe/d3dx_texture_hook_fixture.cpp (MinGW i686, run natively through WSL interop)
imports the system d3dx9_43.dll as X3FL.exe does, resolves and exchanges its own D3DXCreateTextureFromFileInMemoryEx
import slot with the production unit, and drives real D3DX texture loads on a hidden-window HAL device: the observer
sees a marked atlas, unmarked and failing calls forward with the direct original's HRESULT, LastError, MXCSR and x87
control word; a probe original proves the CPU-state boundary both ways; restore() and a failed protection restore roll
the slot back; synthetic section-view images prove every refusal (slot_not_writable on a read-only view) and the late
window. (b) Edition wiring: the fl target links the unit, AP keeps its loading_trace row and both share
observe_created, and capture.cpp logs the FL row and texture_hook from the hook state. (c) The import census of the
shipped X3FL.exe (verification/results/fl-material-strength/import_census_out.txt) names exactly one resolvable slot.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
UNIT = ROOT / 'src/proxy/d3dx_texture_hook.cpp'
HEADER = ROOT / 'src/proxy/d3dx_texture_hook.h'
FIXTURE = ROOT / 'verification/probe/d3dx_texture_hook_fixture.cpp'
CENSUS = ROOT / 'verification/results/fl-material-strength/import_census_out.txt'


def build_and_run(tmp):
    """Builds the fixture from the production unit and runs it natively; None without the toolchain or interop."""
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
        return None
    tmp = Path(tmp)
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-msse2', '-mfpmath=sse',
              '-mstackrealign', '-mincoming-stack-boundary=2', '-DX3M_EDITION_FL=1', '-DX3M_D3DX_TEXTURE_HOOK_FIXTURE']
    objects = []
    for source, flags in (('src/proxy/d3dx_texture_hook.cpp', ['-fno-exceptions']),
                          ('src/ownership/application_admission_abi.cpp', ['-fno-exceptions']),
                          ('src/ownership/application_admission.cpp', []), ('src/proxy/engine_patch.cpp', [])):
        obj = tmp / (Path(source).stem + '.o')
        subprocess.run([compiler, *common, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = tmp / 'd3dx_texture_hook_fixture.exe'
    subprocess.run([compiler, *common, '-static', str(FIXTURE), *objects, '-ld3d9', '-ld3dx9_43', '-o', str(exe)],
                   check=True)
    return subprocess.run([str(exe)], text=True, capture_output=True, timeout=120)


class FlTextureHookNativeFixture(unittest.TestCase):
    def test_import_slot_hook_observer_forwarding_rollback_and_refusals(self):
        with tempfile.TemporaryDirectory(prefix='x3-fl-texture-hook-') as tmp:
            result = build_and_run(tmp)
        if result is None:
            self.skipTest('needs i686-w64-mingw32-g++ and WSL interop (a native Windows run)')
        out = result.stdout
        self.assertEqual(result.returncode, 0, '\n'.join(l for l in out.splitlines() if l.startswith('FAIL'))[:4000]
                         + result.stderr[-1000:])
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0')
        self.assertGreaterEqual(int(re.search(r'RESULT PASS checks=(\d+)', out).group(1)), 55)
        resolved = re.search(r'RESOLVED dll=(\S+) slot_rva=(0x[0-9a-f]+) imp_rva=(0x[0-9a-f]+) reason=ok', out)
        self.assertIsNotNone(resolved)
        self.assertEqual(resolved.group(1).lower(), 'd3dx9_43.dll')
        self.assertEqual(resolved.group(2), resolved.group(3))
        self.assertRegex(out, r'MARKED hr=00000000 .* observer_calls=1 lane=1')
        self.assertRegex(out, r'FAILED hr=8876086c error=00c0ffee direct_error=00c0ffee')
        self.assertIn('BOUNDARY result=00000001 entry_error=00c0ffee entry_mxcsr=00007f80 entry_cw=0e7f entry_top=7 '
                      'exit_error=0000beef exit_mxcsr=00009fc0 exit_cw=037f exit_top=6', out)
        self.assertIn('ROLLBACK failing_restores=1 reason=protect_restore_failed slot_original=1', out)
        self.assertIn('ROLLBACK failing_restores=2 reason=protect_restore_failed slot_original=1', out)
        self.assertIn('REFUSAL reason=foreign_redirect', out)
        self.assertIn('CHAINED reason=foreign_chained slot_foreign=1 stub_forwards=1 observer_delta=0', out)
        reasons = dict(re.findall(r'SYNTHETIC case=(\S+) reason=(\S+)', out))
        self.assertEqual(reasons, {
            'upper_case_dll': 'ok', 'read_only_view': 'slot_not_writable', 'absent': 'import_absent',
            'not_d3dx9': 'import_absent', 'no_name_table': 'import_absent', 'ambiguous': 'ambiguous_import',
            'dll_not_loaded': 'dll_not_loaded', 'misaligned': 'slot_misaligned', 'foreign': 'foreign_redirect',
            'bad_image': 'bad_image'})
        self.assertIn('LATE reason=late_claim', out)


class FlTextureHookWiring(unittest.TestCase):
    def test_editions(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        ap = re.search(r'set\(X3M_AP_SOURCES(.*?)\)\n', cmake, re.S).group(1)
        self.assertNotIn('d3dx_texture_hook.cpp', ap)  # AP keeps loading_trace's row
        fl = cmake[cmake.index('add_library(d3d9 SHARED ${X3M_SHARED_SOURCES}'):cmake.index('x3m_add_fog_field_assets')]
        self.assertIn('target_sources(d3d9 PRIVATE src/proxy/d3dx_texture_hook.cpp)', fl)
        self.assertRegex(cmake, r'set_source_files_properties\(src/proxy/d3dx_texture_hook\.cpp PROPERTIES '
                                r'COMPILE_OPTIONS "-fno-exceptions"\)')
        trace = (ROOT / 'src/proxy/loading_trace.cpp').read_text()
        self.assertIn('d3dx_texture_hook::observe_created(texture_observer, d, data, size, result, out, error);',
                      trace)
        self.assertIn('observe_created(observer_.load(std::memory_order_acquire), d, data, size, result, out, '
                      'GetLastError());', UNIT.read_text())
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('d3dx_texture_hook::install(GetModuleHandleW(nullptr), texture_created)', capture)
        self.assertIn('lod_material_strength_texture_hook state=refused reason=%s', capture)
        self.assertIn('unsigned(d3dx_texture_hook::installed())', capture)
        self.assertNotIn('texture_hook=0"', capture)
        # No hard-coded d3dx9 version in the lookup.
        self.assertNotRegex(UNIT.read_text(), r'"d3dx9_\d+')

    def test_fl_census(self):
        text = CENSUS.read_text()
        fl = text[text.index('exe=/mnt/c/X3FL/X3FL.exe'):]
        fl = fl[:fl.index('\nexe=')] if '\nexe=' in fl else fl
        self.assertIn('target=D3DXCreateTextureFromFileInMemoryEx dll=d3dx9_43.dll slot_rva=0x00150374 '
                      'by_name_table=1', fl)
        self.assertIn('resolvable=1 matches=1', fl)
        self.assertIn('dynamic_base=1', fl)


if __name__ == '__main__':
    unittest.main()
