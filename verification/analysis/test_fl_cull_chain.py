"""Host tests of the FL cull/LOD selector ports (src/engine/fl/cull_small_parts_fl.cpp, terran_station_lod_fl.cpp,
lod_occlusion_fl.cpp; cull_small_parts_sites_fl.h, lod_patch_sites_fl.h).

docs/verification/cull-small-parts.md, "FL port (2026-11-02, not flown)"; sites and ABI: docs/reverse-engineering/fl/
cull-chain.md, "Verified 2026-11-02 against X3FL.exe". (a) The native fixture verification/probe/fl_cull_chain.cpp
(MinGW i686, run natively through WSL interop) lays the three FL windows, the marker and JET writers into a 64 KB-aligned
private image, installs with the production modules and runs the real window bytes from the site with the FL selector's
frame: the stub's decisions for small, big, dock, projectile and far-jet nodes against the unpatched image (ECX live and
kept, EAX/EDX as the native max-threshold block leaves them at 0x0961f3, the native flags at both continuations, ESP/EBP,
locals, DF, LastError), the three stub variants, every single-bit window change refused (the occlusion fixup zeroed or
rebased too), read-back / chain / restore faults, shutdown, initialize()'s rows and refusals, the frame side, the two
one-byte patches' executed paths, and the late-window refusal. The emitted stubs are walked instruction by instruction
(verification/results/fl-cull-chain/stub_cpu_state_audit.py). (b) Edition wiring: the CMake lists, the capture.cpp
un-fencing, the far-jet handler's FL model offset and the FL constants against the verified contract.json.
"""
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROVIDER = ROOT / 'src/engine/fl/cull_small_parts_fl.cpp'
SITES = ROOT / 'src/engine/fl/cull_small_parts_sites_fl.h'
LOD_SITES = ROOT / 'src/engine/fl/lod_patch_sites_fl.h'
FIXTURE = ROOT / 'verification/probe/fl_cull_chain.cpp'
SHIM = ROOT / 'verification/probe/fl_cull_chain_shim.h'
AUDIT = ROOT / 'verification/results/fl-cull-chain/stub_cpu_state_audit.py'
CONTRACT = ROOT / 'verification/results/fl-cull-chain/contract.json'


def build_and_run(tmp):
    """Builds the fixture from the production sources, runs it natively and audits the emitted stubs. Returns (run,
    audit) CompletedProcess objects; None when the toolchain or interop is absent
    (verification/results/fl-cull-chain/mutation_check.py calls it over mutated sources)."""
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
        return None
    tmp = Path(tmp)
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
              '-mincoming-stack-boundary=2', '-DX3M_EDITION_FL=1', '-msse2', '-mfpmath=sse',
              '-DX3M_CULL_SMALL_PARTS_FL_FIXTURE', '-DX3M_LOD_PATCHES_FL_FIXTURE']
    objects = []
    units = (('src/engine/fl/cull_small_parts_fl.cpp',
              ['-fno-exceptions', '-include', str(SHIM), '-DX3M_CULL_SMALL_PARTS_FL_SHIM']),
             ('src/engine/fl/terran_station_lod_fl.cpp', ['-fno-exceptions']),
             ('src/engine/fl/lod_occlusion_fl.cpp', ['-fno-exceptions']),
             ('src/proxy/engine_patch.cpp', []), ('src/engine/fl/executable_identity_fl.cpp', []))
    for source, flags in units:
        obj = tmp / (Path(source).stem + '.o')
        subprocess.run([compiler, *common, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = tmp / 'fl_cull_chain.exe'
    subprocess.run([compiler, *common, '-static', str(FIXTURE), *objects, '-o', str(exe)], check=True)
    run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=180)
    out = tmp / 'fixture.out'
    out.write_text(run.stdout)
    audit = subprocess.run(['python3', str(AUDIT), str(out)], text=True, capture_output=True)
    return run, audit


class FlCullChainNativeFixture(unittest.TestCase):
    def test_stub_patches_install_rollback_and_late_window(self):
        with tempfile.TemporaryDirectory(prefix='x3-fl-cull-') as tmp:
            outcome = build_and_run(tmp)
        if outcome is None:
            self.skipTest('needs i686-w64-mingw32-g++ and WSL interop (a native Windows run)')
        result, audit = outcome
        out = result.stdout
        self.assertEqual(result.returncode, 0, '\n'.join(l for l in out.splitlines() if l.startswith('FAIL'))[:4000]
                         + result.stderr[-1000:])
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0')
        self.assertGreaterEqual(int(re.search(r'RESULT PASS checks=(\d+)', out).group(1)), 150)
        self.assertRegex(out, r'WINDOWS cull_fnv1a=9df9cf9e terran_fnv1a=0e8e0d62 occlusion_fnv1a_masked=3105ccaa '
                              r'marker_store=7fd6e34b marker_or=147c2006 jet_writer=4d34a219 ')
        self.assertIn('FLIPS cull=440 refused=440', out)
        self.assertIn('FLIPS terran=248 refused=248 occlusion=160 refused=160 fixup=32 fixup_refused=32', out)
        self.assertIn('DECISIONS culled=20 dock=10 exempt=6 far=3 native=41', out)
        self.assertEqual(len(re.findall(r'^LOG cull_small_parts requested=\S+ px=\S+ patched=[01] edition=fl state=',
                                        out, re.M)), 8)
        self.assertEqual(audit.returncode, 0, audit.stdout[-3000:] + audit.stderr[-1000:])
        variants = json.loads(audit.stdout)['variants']
        self.assertEqual({k: (v['calls'], v['exits']['continue'] > 0, v['exits']['cull'] > 0) for k, v in variants.items()},
                         {'full': (1, True, True), 'no_projectiles': (1, True, True), 'no_far': (0, True, True)})


def strip(text):
    return re.sub(r'//.*', '', text)


class FlCullChainWiring(unittest.TestCase):
    def test_constants_match_the_verified_contract(self):
        contract = json.loads(CONTRACT.read_text())
        windows = contract['windows']
        sites, lod = SITES.read_text(), LOD_SITES.read_text()
        self.assertEqual(windows['shared_cull_window']['fl']['fnv1a32'], '9df9cf9e')
        self.assertEqual(windows['shared_cull_window']['fl']['start_rva'], '0x0961c5')
        self.assertEqual(windows['shared_cull_window']['fl']['length'], 55)
        self.assertIn('window_fnv1a = 0x9df9cf9e', sites)
        for key, value in (('projectile_store', '7fd6e34b'), ('projectile_or', '147c2006'), ('jet_writer', '4d34a219')):
            self.assertEqual(windows[key]['fl']['fnv1a32'], value)
            self.assertIn('0x' + value, sites)
        self.assertEqual(windows['terran_root_flag']['fl']['fnv1a32'], '0e8e0d62')
        self.assertEqual(windows['lod_occlusion_gate']['fl']['fnv1a32'], '3105ccaa')
        self.assertEqual(windows['lod_occlusion_gate']['fl']['relocated_dword_rvas'], ['0x0e56bc'])
        self.assertEqual(windows['lod_occlusion_gate']['fl']['relocated_targets_rva'], ['0x1a9014'])
        self.assertIn('{0x095e82, 31, terran_bytes, 0, 0, 0x16, 0xc1, 0xc0, 0x0e8e0d62}', lod)
        self.assertIn('{0x0e56b1, 20, occlusion_bytes, 0x0b, 0x1a9014, 0x10, 0x6f, 0x00, 0x3105ccaa}', lod)
        self.assertEqual(contract['edits']['terran'], {'after': 0xc0, 'before': 0xc1, 'result': 'cmovl eax, eax',
                                                       'rva': 0x095e98})
        self.assertEqual(contract['edits']['occlusion']['rva'], 0x0e56c1)
        self.assertEqual((contract['edits']['occlusion']['before'], contract['edits']['occlusion']['after']), (0x6f, 0))
        for rva in ('0x0961d3', '0x0961dc', '0x0961f3', '0x049b1d', '0x04a807', '0x03deb3'):
            self.assertIn(rva, sites)
        self.assertIn('model_offset = 0x144', sites)

    def test_edition_lists_and_capture(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        linked = re.search(r'set\(X3M_AP_ENGINE_LINKED_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        ap = re.search(r'set\(X3M_AP_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        self.assertNotIn('cull_small_parts.cpp', linked)   # AP only now: the fl target links its own provider
        self.assertIn('src/proxy/engine_far_jets.cpp', linked)
        for source in ('cull_small_parts.cpp', 'terran_station_lod.cpp', 'lod_occlusion.cpp'):
            self.assertIn('src/proxy/' + source, ap)
        self.assertIn('target_sources(d3d9 PRIVATE src/engine/fl/cull_small_parts_fl.cpp src/engine/fl/terran_station_lod_fl.cpp\n'
                      '    src/engine/fl/lod_occlusion_fl.cpp)', cmake)
        capture = strip((ROOT / 'src/proxy/capture.cpp').read_text())
        start = capture.index('    engine_effects_patch::initialize();\n    \n')
        fl = capture[start:capture.index('#endif', start)]
        self.assertLess(fl.index('terran_station_lod::initialize();'), fl.index('lod_occlusion::initialize();'))
        self.assertLess(fl.index('lod_occlusion::initialize();'), fl.index('cull_small_parts::initialize();'))
        # The frame side: an #else branch beside each AP call (AP order untouched).
        for call in ('cull_small_parts::present(ctx.id, ctx.frame, ctx.capture);', 'cull_small_parts::begin_frame();',
                     'cull_small_parts::after_reset(p ? p->BackBufferWidth : 0u);',
                     'cull_small_parts::set_backbuffer_width(p->BackBufferWidth);'):
            self.assertEqual(capture.count(call), 2, call)
            second = capture.rindex(call)
            self.assertIn('#else', capture[second - 120:second])
            self.assertIn('#endif', capture[second:second + 80])

    def test_provider_contract(self):
        code = strip(PROVIDER.read_text())
        self.assertNotIn('0x400000', code)
        self.assertNotIn('0x0047d2a2', code)
        self.assertIn('x3m::fl::module_base()', code)
        self.assertIn('engine_patch::install_window_open()', code)
        self.assertIn('spec.length = S::site_length;', code)
        self.assertIn('if (const char* refused = S::plan(window)) return done(refused);', code)
        self.assertIn('return done(roll_back("readback_rolled_back"));', code)
        self.assertIn('"cull_small_parts requested=%s px=%.4g patched=%u edition=fl state=%s reason=%s site=0x%08lx '
                      'rva=0x%06lx cull=0x%08lx write=%s', code)
        far = strip((ROOT / 'src/proxy/engine_far_jets.cpp').read_text())
        self.assertIn('#if X3M_EDITION_FL\nconstexpr unsigned model_at = 0x144;', far)
        self.assertIn('word(node, model_at)', far)
        for name in ('terran_station_lod_fl.cpp', 'lod_occlusion_fl.cpp'):
            text = strip((ROOT / 'src/engine/fl' / name).read_text())
            self.assertIn('x3m::fl::module_base()', text)
            self.assertIn('engine_patch::install_window_open()', text)
            self.assertIn('edition=fl state=%s reason=%s', text)
            self.assertNotIn('WriteFile', text)


if __name__ == '__main__':
    unittest.main()
