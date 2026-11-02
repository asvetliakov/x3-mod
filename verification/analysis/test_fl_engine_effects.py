"""Host tests of the FL engine-effects call redirects (src/engine/fl/engine_effects_fl.cpp, engine_effects_sites_fl.h).

docs/architecture/engine-effects-modern.md, "FL port, step 1"; sites and ABI: docs/reverse-engineering/fl/
engine-effects.md, "Verified 2026-11-02 against X3FL.exe". (a) The native fixture verification/probe/fl_engine_effects.cpp
(MinGW i686, run natively through WSL interop) lays the two FL windows into a 64 KB-aligned private image (B's fixup
relocated to that base), claims both calls with the production module and engine_patch::claim_call, and checks: class 7
skipped and class 10 forwarded through each stub with the exact register/stack contract (A: ECX/EDX and eight dwords,
caller pop; B: four dwords, RET 0x10), EBX/ESI/EDI/EBP/ESP/DF/LastError kept at the caller, the write paths (B atomic,
A plain), every single-bit window change refused (B's fixup zeroed or rebased too), read-back rollbacks (both or none),
a refused restore kept registered, shutdown, initialize()'s rows and refusals, and the late-window refusal. The stubs are
audited instruction by instruction (verification/results/fl-engine-effects/stub_cpu_state_audit.py). (b) Edition
wiring: the CMake lists, the capture.cpp un-fencing and the FL constants against the verified contract.json.
"""
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROVIDER = ROOT / 'src/engine/fl/engine_effects_fl.cpp'
SITES = ROOT / 'src/engine/fl/engine_effects_sites_fl.h'
FIXTURE = ROOT / 'verification/probe/fl_engine_effects.cpp'
SHIM = ROOT / 'verification/probe/engine_effects_patch_fixture_shim.h'
AUDIT = ROOT / 'verification/results/fl-engine-effects/stub_cpu_state_audit.py'
CONTRACT = ROOT / 'verification/results/fl-engine-effects/contract.json'


def build_and_run(tmp, provider=None):
    """Builds the fixture from the production sources (CMake per-file flags), runs it natively and audits the stubs.
    Returns (run, audit) CompletedProcess objects; None when the toolchain or interop is absent. provider replaces
    engine_effects_fl.cpp (a mutated copy beside it: verification/results/fl-engine-effects/mutation_check.py)."""
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
        return None
    tmp = Path(tmp)
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
              '-mincoming-stack-boundary=2', '-DX3M_EDITION_FL=1', '-DX3M_ENGINE_EFFECTS_FL_FIXTURE', '-msse2',
              '-mfpmath=sse']
    objects = []
    for source, flags in ((provider or 'src/engine/fl/engine_effects_fl.cpp',
                           ['-fno-exceptions', '-include', str(SHIM), '-DX3M_ENGINE_EFFECTS_SHIM']),
                          ('src/proxy/engine_patch.cpp', []), ('src/engine/fl/executable_identity_fl.cpp', [])):
        obj = tmp / (Path(source).stem + '.o')
        subprocess.run([compiler, *common, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = tmp / 'fl_engine_effects.exe'
    subprocess.run([compiler, *common, '-static', str(FIXTURE), *objects, '-o', str(exe)], check=True)
    run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=120)
    audit = subprocess.run(['python3', str(AUDIT), str(exe)], text=True, capture_output=True)
    return run, audit


class FlEngineEffectsNativeFixture(unittest.TestCase):
    def test_stubs_install_rollback_and_late_window(self):
        with tempfile.TemporaryDirectory(prefix='x3-fl-ee-') as tmp:
            outcome = build_and_run(tmp)
        if outcome is None:
            self.skipTest('needs i686-w64-mingw32-g++ and WSL interop (a native Windows run)')
        result, audit = outcome
        out = result.stdout
        self.assertEqual(result.returncode, 0, '\n'.join(l for l in out.splitlines() if l.startswith('FAIL'))[:4000]
                         + result.stderr[-1000:])
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0')
        self.assertGreaterEqual(int(re.search(r'RESULT PASS checks=(\d+)', out).group(1)), 50)
        self.assertIn('FLIPS a=328 refused_a=328 b=336 refused_b=336 fixup=32 fixup_refused=32', out)
        self.assertRegex(out, r'WINDOWS a_fnv1a=5a69143c b_fnv1a_masked=601f528c ')
        self.assertEqual(len(re.findall(r'^LOG engine_effects_patch site=[AB] va=00000000 rva=0150(?:75|aa) edition=fl ',
                                        out, re.M)), 10)
        self.assertEqual(len(re.findall(r'^LOG engine_effects_patch site=[AB] va=[0-9a-f]{8} rva=0150(?:75|aa) edition=fl '
                                        r'state=active reason=ok mode=plumes setting=- write=(?:plain|atomic)$', out, re.M)), 2)
        self.assertEqual(audit.returncode, 0, audit.stdout[-2000:] + audit.stderr[-1000:])
        stubs = json.loads(audit.stdout)['stubs']
        self.assertEqual({k: v['instructions'] for k, v in stubs.items()},
                         {'_x3m_engine_effects_stub_a': 8, '_x3m_engine_effects_stub_b': 8})


def strip(text):
    return re.sub(r'//.*', '', text)


class FlEngineEffectsWiring(unittest.TestCase):
    def test_constants_match_the_verified_contract(self):
        sites = SITES.read_text()
        contract = json.loads(CONTRACT.read_text())
        text = json.dumps(contract)
        for value in ('5a69143c', '601f528c', '625a39c5'):
            self.assertIn(value, text)
            self.assertIn('0x' + value, sites)
        for rva in ('0x015054', '0x015075', '0x0150e0', '0x015085', '0x0150aa', '0x013640', '0x1a83b0', '0x1aa100',
                    '0x0fff26'):
            self.assertIn(rva, sites)

    def test_edition_lists_and_capture(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        linked = re.search(r'set\(X3M_AP_ENGINE_LINKED_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        ap = re.search(r'set\(X3M_AP_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        self.assertNotIn('engine_effects_patch.cpp', linked)   # AP only: the fl target links its own provider
        self.assertIn('src/proxy/engine_effects.cpp', linked)    # shared: the option, identity and body resolver
        self.assertIn('src/proxy/engine_effects_patch.cpp', ap)
        self.assertIn('target_sources(d3d9 PRIVATE src/engine/fl/engine_effects_fl.cpp)', cmake)
        capture = strip((ROOT / 'src/proxy/capture.cpp').read_text())
        fl = capture[capture.index('#if X3M_EDITION_FL\n    \n    \n    \n    engine_effects::initialize();'):]
        fl = fl[:fl.index('#endif')]
        self.assertLess(fl.index('engine_effects::initialize();'), fl.index('engine_effects_patch::initialize();'))
        # begin_frame outside every fence (the AP-only lens_flare_cull that followed it went on 2026-11-02).
        frame = capture[capture.index('engine_effects::begin_frame(ctx.frame);') - 200:]
        self.assertIn('#endif', frame[:200])
        self.assertNotIn('lens_flare_cull', capture)

    def test_provider_contract(self):
        code = strip(PROVIDER.read_text())
        self.assertNotIn('0x400000', code)
        for va in ('0x415075', '0x4150aa', '0x4150e0', '0x413640', '0x5a83b0', '0x4147eb', '0x41482c'):
            self.assertNotIn(va, code.lower())
        self.assertIn('x3m::fl::module_base()', code)
        self.assertIn('engine_patch::install_window_open()', code)
        self.assertIn('S::plan_b(b_bytes, base)', code)
        # B claimed first (atomic), A only with B live; a failed A restores B.
        self.assertLess(code.index('claim_call(site_[1], base + S::b_site_rva'), code.index('claim_call(site_[0], base + S::a_site_rva'))
        self.assertIn('const bool b_back = engine_patch::restore_call(site_[1]);', code)
        self.assertIn('"engine_effects_patch site=%c va=%08lx rva=%06lx edition=fl state=%s reason=%s mode=%s setting=%s write=%s"', code)
        stubs = code[code.index('asm(R"('):code.index(')");')]
        self.assertNotIn('ecx', stubs.replace('jmp dword ptr', ''))
        self.assertNotIn('edx', stubs)
        self.assertIn('mov eax, dword ptr [esp+0xc]', stubs)
        self.assertIn('mov eax, dword ptr [esp+0x10]', stubs)
        self.assertIn('ret 0x10', stubs)

    def test_shared_fl_branches(self):
        effects = (ROOT / 'src/proxy/engine_effects.cpp').read_text()
        self.assertIn('body_global_ = base ? base + flee::body_slot_rva : 0;', effects)
        self.assertIn('const std::uint64_t epoch = object_lifetime::stats().load_epoch;', effects)
        plumes = (ROOT / 'src/proxy/motion_output_engine_plumes_inc.h').read_text()
        self.assertIn('engine_nozzle_source_ = engine_nozzle::core::Source::draw;', plumes)


if __name__ == '__main__':
    unittest.main()
