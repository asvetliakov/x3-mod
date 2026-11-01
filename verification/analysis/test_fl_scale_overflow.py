"""Host tests of the FL scaler overflow fix (src/engine/fl/scale_overflow_fix_fl.cpp).

docs/architecture/farnhams-legacy-scale-overflow.md. (a) The generated fragment (scale_overflow_fix_fl_inc.h)
equals the generator's output for X3FL.exe on disk: the 16 entry bytes, the body shape and its FNV-1a, no base
relocation in the body, no relocated pointer and no rel8/rel32 branch from outside into its interior, the eleven
direct callers; the generator refuses a changed body, an interior jump and a moved caller. (b) The capstone
witness (verification/results/fl-scale-overflow/abi_witness.py): the helper's ABI and each caller's argument
setup. (c) The native fixture verification/probe/fl_scale_overflow.cpp (MinGW i686, run natively through WSL
interop) on the original body bytes at a synthetic base: the original traps on the FL run 3 vector at its third
idiv, the replacement clamps z to INT32_MAX and matches the original bit for bit (out, EAX, ECX, EDX) wherever
the original does not trap, with CPU state, rows, restore and late-window refusal checked; its per-call path is
audited for x87/XMM use. (d) Edition wiring. X3FL.exe is only read; its bytes stay in a temporary file.
"""
import importlib.util
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FL_EXE = Path('/mnt/c/X3FL/X3FL.exe')
FRAGMENT = ROOT / 'src/engine/fl/scale_overflow_fix_fl_inc.h'
PROVIDER = ROOT / 'src/engine/fl/scale_overflow_fix_fl.cpp'
FIXTURE = ROOT / 'verification/probe/fl_scale_overflow.cpp'
RESULTS = ROOT / 'verification/results/fl-scale-overflow'

spec = importlib.util.spec_from_file_location('fl_scale_overflow_constants', ROOT / 'tools/analysis/fl_scale_overflow_constants.py')
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)


def need(path):
    if not path.exists():
        raise unittest.SkipTest(f'{path} absent')
    return path.read_bytes()


def constants():
    text = FRAGMENT.read_text()
    num = lambda name: int(re.search(rf'\b{name} = (0x[0-9a-f]+|\d+)', text).group(1), 0)
    entry = bytes(int(b, 16) for b in re.search(r'helper_entry\[\d+\] = \{([^}]*)\}', text).group(1).split(','))
    return {'helper_rva': num('helper_rva'), 'helper_length': num('helper_length'), 'displaced': num('displaced_length'),
            'fnv1a': num('helper_fnv1a'), 'entry': entry}


class FlScaleConstants(unittest.TestCase):
    def test_fragment_is_generated_from_x3fl(self):
        data = need(FL_EXE)
        self.assertEqual(FRAGMENT.read_text(), gen.fragment(data, gen.identity.parse(data)))

    def test_documented_helper_and_callers(self):
        data = need(FL_EXE)
        fields = gen.identity.parse(data)
        d = gen.decode(data, fields)
        c = constants()
        self.assertEqual((c['helper_rva'], c['helper_length'], c['displaced']), (0x12d10, 0xbc, 6))
        self.assertEqual(c['entry'], bytes.fromhex('558bec83e4f883ec10568b750c578bfa'))
        self.assertEqual(c['fnv1a'], gen.fnv1a(d['body']))
        self.assertEqual(d['callers'], [0x170f6, 0x22dd4, 0x22e2a, 0x4fc00, 0x4fd07, 0x64075, 0x64f3d, 0x84805,
                                        0x84870, 0xa4d55, 0xa4da5])
        # The one byte-scan candidate is the low byte of a relocated imm32 (push 0x54cae0 at 0x12ddb).
        self.assertEqual(d['operand_candidates'], [0x12ddc])
        # The entry JMP lies inside one aligned qword (an RVA keeps that at a 64K-aligned base): lock cmpxchg8b.
        self.assertLessEqual(c['helper_rva'] % 8 + 5, 8)

    def test_generator_refuses_mutations(self):
        data = need(FL_EXE)
        fields = gen.identity.parse(data)
        off = lambda rva: gen.identity.rva_to_offset(fields, rva)
        idiv = bytearray(data)
        idiv[off(0x12dbf + 1)] = 0xf8  # idiv ecx -> idiv eax
        jump = bytearray(data)
        jump[off(0x12dd0):off(0x12dd0) + 2] = b'\xeb\xc0'  # jmp short 0x12d92, into the interior
        caller = bytearray(data)
        caller[off(0x22dd4)] = 0x90  # one direct caller fewer
        for mutated in (idiv, jump, caller):
            with self.assertRaises(ValueError):
                gen.decode(bytes(mutated), fields)


class FlScaleAbiWitness(unittest.TestCase):
    def test_helper_abi_and_caller_setup(self):
        need(FL_EXE)
        try:
            import capstone  # noqa: F401
        except ImportError:
            self.skipTest('capstone is required')
        run = subprocess.run(['python3', str(RESULTS / 'abi_witness.py'), str(FL_EXE)], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stdout[-2000:] + run.stderr[-2000:])
        w = json.loads(run.stdout)
        h = w['helper']
        self.assertEqual((h['instructions'], h['branches'], h['divides']), (73, 5, ['12d4c', '12d83', '12dbf']))
        self.assertEqual(h['exits'], ['12db0 ret', '12dcb ret'])
        self.assertEqual(h['written'], ['eax', 'ebp', 'ecx', 'edi', 'edx', 'esi', 'esp'])  # EBX untouched
        # The only argument operands: [ebp+8] (out) and [ebp+0xc] (&c); a, b come in ECX, EDX.
        self.assertEqual([m for m in h['memory_operands'] if m.startswith('[ebp')], ['[ebp + 0xc]', '[ebp + 8]'])
        self.assertEqual(len(w['callers']), 11)
        for c in w['callers']:
            self.assertRegex(c['ecx'], r' (lea ecx|add ecx, 0x100)', c['site'])  # a pointer: a local or object + 0x100
            self.assertRegex(c['edx'], r' lea edx, \[e[bs]p ', c['site'])       # &b: a local
            self.assertEqual(len(c['pushes']), 2, c['site'])                        # &c, then out
            self.assertTrue(c['after']['eflags'].startswith('write'), c['site'])
            self.assertFalse(c['after']['ecx'].startswith('read'), c['site'])
            self.assertFalse(c['after']['edx'].startswith('read'), c['site'])
            self.assertRegex(c['after']['eax'], r'^read .*\[eax\]', c['site'])     # the caller loads out through EAX
        self.assertEqual(w['interior'], {'hits': [], 'operand_candidates': ['12ddc inside 12ddb push 0x54cae0'],
                                         'relocations_in_helper': []})


def build_and_run(tmp):
    """Builds the fixture from the production sources with the CMake per-file flags, runs it natively on the
    helper's body bytes (a temporary file) and audits its per-call path. Returns (run, audit); None when the
    toolchain, interop or X3FL.exe is absent. Also used by verification/results/fl-scale-overflow/run_fixture.py."""
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists() or not FL_EXE.exists():
        return None
    body = Path(tmp) / 'helper_body.bin'
    data = FL_EXE.read_bytes()
    body.write_bytes(gen.decode(data, gen.identity.parse(data))['body'])
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
              '-mincoming-stack-boundary=2', '-DX3M_EDITION_FL=1', '-DX3M_FL_SCALE_FIX_FIXTURE']
    light = ['-fno-exceptions', '-mno-sse', '-mno-mmx', '-mfpmath=387']
    sse = ['-msse2', '-mfpmath=sse']
    objects = []
    for source, flags in (('src/engine/fl/scale_overflow_fix_fl.cpp', light), ('src/proxy/engine_patch.cpp', sse),
                          ('src/engine/fl/executable_identity_fl.cpp', sse)):
        obj = Path(tmp) / (Path(source).stem + '.o')
        subprocess.run([compiler, *common, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = Path(tmp) / 'fl_scale_overflow.exe'
    subprocess.run([compiler, *common, *sse, '-static', str(FIXTURE), *objects, '-o', str(exe)], check=True)
    windows_body = subprocess.run(['wslpath', '-w', str(body)], capture_output=True, text=True, check=True).stdout.strip()
    run = subprocess.run([str(exe), windows_body], text=True, capture_output=True, timeout=300)
    audit = subprocess.run(['python3', str(RESULTS / 'cpu_state_audit.py'), str(exe)], text=True, capture_output=True)
    return run, audit


class FlScaleNativeFixture(unittest.TestCase):
    def test_replacement_parity_saturation_cpu_state_rows_restore_and_late_window(self):
        with tempfile.TemporaryDirectory(prefix='x3-fl-scale-') as tmp:
            outcome = build_and_run(tmp)
        if outcome is None:
            self.skipTest('needs i686-w64-mingw32-g++, WSL interop and X3FL.exe')
        result, audit = outcome
        out = result.stdout
        self.assertEqual(result.returncode, 0, out[-4000:] + result.stderr[-1000:])
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0')
        self.assertNotIn('FAIL ', out)
        self.assertIn('GATE flips=188 refused=188 entry=16 body=172', out)
        self.assertIn('ORIGINAL_DUMP trapped=1 code=0xc0000095 offset=0xaf', out)
        self.assertIn('REPLACEMENT_DUMP out=683350000,-346272000,2147483647 ecx=2 edx=0 saturated=1', out)
        self.assertRegex(out, r'SWEEP cases=20000 fit=\d+ c0=\d+ clamped=\d+ mismatches=0')
        self.assertRegex(out, r'TIMING original_ns=[\d.]+ replacement_ns=[\d.]+')
        # The first due period after install (saturated=0, the call rate), then the one after the clamp.
        samples = re.findall(r'^LOG fl\.scale_fix_sample .*$', out, re.M)
        self.assertEqual(len(samples), 2)
        self.assertIn(' saturated=0 ', samples[0])
        self.assertIn(' saturated=1 last_site=0x22dd4', samples[1])
        self.assertEqual(audit.returncode, 0, audit.stdout[-2000:] + audit.stderr[-1000:])


class FlScaleWiring(unittest.TestCase):
    def test_edition_lists_and_loader(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        for group in ('X3M_AP_SOURCES', 'X3M_SHARED_SOURCES', 'X3M_AP_ENGINE_LINKED_SOURCES'):
            self.assertNotIn('scale_overflow', re.search(rf'set\({group}\n(.*?)\)\n', cmake, re.S).group(1))
        fl_branch = cmake[cmake.index('if(X3M_EDITION STREQUAL "ap")'):]
        fl_branch = fl_branch[fl_branch.index('else()'):fl_branch.index('endif()')]
        self.assertIn('target_sources(d3d9 PRIVATE src/engine/fl/scale_overflow_fix_fl.cpp)', fl_branch)
        self.assertIn('set_source_files_properties(src/engine/fl/scale_overflow_fix_fl.cpp\n'
                      '  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-mno-sse;-mno-mmx;-mfpmath=387")', cmake)
        loader = (ROOT / 'src/proxy/loader.cpp').read_text()
        fl_block = loader[loader.index('#if X3M_EDITION_FL\n    // FL edition'):]
        fl_block = fl_block[:fl_block.index('#endif')]
        self.assertIn('x3m::fl::scale_fix::initialize();', fl_block)
        self.assertLess(fl_block.index('object_trace::initialize();'), fl_block.index('scale_fix::initialize();'))

    def test_provider_contract(self):
        code = re.sub(r'//.*', '', PROVIDER.read_text())
        self.assertNotIn('0x400000', code)
        self.assertNotIn('0x412d10', code.lower())
        self.assertIn('x3m::fl::module_base()', code)
        self.assertIn('x3m::fl::verified()', code)
        self.assertIn('#include "scale_overflow_fix_fl_inc.h"', code)
        self.assertIn('engine_patch::claim(site_, spec)', code)
        self.assertIn('engine_patch::push_front', code)
        self.assertIn('engine_patch::install_window_open()', code)
        for status in ('"disabled"', '"executable_mismatch"', '"entry_mismatch"', '"body_mismatch"', '"late_claim"',
                       '"active"', '"chain_failed"'):
            self.assertIn(status, code)
        self.assertIn('"fl.scale_fix status=%s', code)
        self.assertIn('"fl.scale_fix_sample calls=%lu saturated=%lu', code)
        self.assertIn('L"X3M_FL_SCALE_OVERFLOW_FIX"', code)
        self.assertIn('L"X3M_FL_SCALE_OVERFLOW_LOG"', code)
        for banned in ('new ', 'malloc', 'std::string', 'float', 'double'):
            self.assertNotIn(banned, code)
        # The stub keeps the helper's ABI: ECX/EDX in, two stack words, plain RET, ECX/EDX out from the frame.
        self.assertIn('pushl %edx\\n\\tpushl %ecx\\n\\t', code)
        self.assertIn('movl -8(%ebp),%ecx\\n\\tmovl -4(%ebp),%edx\\n\\tleave\\n\\tret', code)


if __name__ == '__main__':
    unittest.main()
