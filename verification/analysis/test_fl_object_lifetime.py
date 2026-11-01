"""Host tests of the FL registry observer (src/engine/fl/object_lifetime_fl.cpp).

docs/architecture/farnhams-legacy-lifetime.md. (a) The generated fragment (object_lifetime_fl_inc.h) equals the
generator's output for X3FL.exe on disk: the insert/remove/destroy entries are six whole instructions with no
relative/absolute operand, no base relocation and no branch into their interior, their functions end only in plain
RETs, insert reads one stack word and remove/destroy none, destroy's displaced CMP is followed by JE, the load call
is the load target's only direct caller, and four engine anchors tie the registry slot (+0xc, handle +0x28) to the
three helpers. A Python mirror of the DLL's gate accepts the image mapped at FL run bases and refuses every
single-byte flip. (b) The native fixture verification/probe/fl_object_lifetime.cpp (MinGW i686, run natively
through WSL interop) drives the real dispatcher through fake helpers with the FL entry bytes and ABI and checks
publication after return, retirement before free, destroy, load epoch, foreign unwind, nesting, ZF into the
replayed destroy prologue, register/EFLAGS/XMM/x87/MXCSR/LastError preservation, baseline, overflow, current()
results, rollback and the late window; its dispatch path is audited for x87 and XMM use. (c) Edition wiring.
X3FL.exe is only read.
"""
import importlib.util
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FL_EXE = Path('/mnt/c/X3FL/X3FL.exe')
FRAGMENT = ROOT / 'src/engine/fl/object_lifetime_fl_inc.h'
PROVIDER = ROOT / 'src/engine/fl/object_lifetime_fl.cpp'
FIXTURE = ROOT / 'verification/probe/fl_object_lifetime.cpp'
AUDIT = ROOT / 'verification/results/fl-object-lifetime/hook_cpu_state_audit.py'
LIVE_BASES = (0x4d0000, 0xfd0000)  # FL runs 1 and 3 (docs/verification/farnhams-legacy.md)


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


gen = load('fl_object_lifetime_constants', ROOT / 'tools/analysis/fl_object_lifetime_constants.py')
trace_test = load('test_fl_object_trace', ROOT / 'verification/analysis/test_fl_object_trace.py')


def need(path):
    if not path.exists():
        raise unittest.SkipTest(f'{path} absent')
    return path.read_bytes()


def constants():
    """The values of the generated C++ fragment."""
    text = FRAGMENT.read_text()
    num = lambda name: int(re.search(rf'\b{name} = (0x[0-9a-f]+|\d+)', text).group(1), 0)
    arr = lambda name: bytes(int(b, 16) for b in re.search(rf'\b{name}\[\d+\] = \{{([^}}]*)\}}', text).group(1).split(','))
    out = {k: num(k) for k in ('engine_slot_rva', 'registry_offset', 'node_handle', 'insert_rva', 'remove_rva',
                               'destroy_rva', 'load_anchor_rva', 'load_site_offset', 'load_target_rva')}
    for k in ('insert_entry', 'remove_entry', 'destroy_entry', 'load_anchor', 'load_target_entry'):
        out[k] = arr(k)
    out['anchors'] = {name: (num(f'{name}_rva'), arr(name), num(f'{name}_operand'))
                      for name in ('remove_anchor', 'destroy_anchor', 'birth_anchor', 'allocate_insert_anchor')}
    return out


def gate(read, base, c):
    """Python mirror of object_lifetime_fl.cpp gate(): None or the refusal."""
    for name in ('insert', 'remove', 'destroy'):
        if read(base + c[f'{name}_rva'], 6) != c[f'{name}_entry']:
            return f'{name}_entry_mismatch'
    call = read(base + c['load_anchor_rva'], len(c['load_anchor']))
    if call != c['load_anchor']:
        return 'load_site_mismatch'
    site = base + c['load_anchor_rva'] + c['load_site_offset']
    if (site + 5 + struct.unpack_from('<I', call, c['load_site_offset'] + 1)[0]) & 0xffffffff != base + c['load_target_rva']:
        return 'load_target_mismatch'
    if read(base + c['load_target_rva'], 6) != c['load_target_entry']:
        return 'load_entry_mismatch'
    for rva, masked, operand in c['anchors'].values():
        actual = read(base + rva, len(masked))
        if actual is None:
            return 'engine_anchor_mismatch'
        actual = bytearray(actual)
        if operand != 0xff:
            if struct.unpack_from('<I', actual, operand)[0] != (base + c['engine_slot_rva']) & 0xffffffff:
                return 'engine_anchor_mismatch'
            actual[operand:operand + 4] = b'\0\0\0\0'
        if bytes(actual) != masked:
            return 'engine_anchor_mismatch'
    return None


class FlLifetimeConstants(unittest.TestCase):
    def test_fragment_is_generated_from_x3fl(self):
        data = need(FL_EXE)
        self.assertEqual(FRAGMENT.read_text(), gen.fragment(data, gen.identity.parse(data)))

    def test_documented_sites_abi_and_anchors(self):
        data = need(FL_EXE)
        d = gen.decode(data, gen.identity.parse(data))
        c = constants()
        self.assertEqual((c['insert_rva'], c['remove_rva'], c['destroy_rva']), (0x10cf30, 0x10d040, 0x10d120))
        self.assertEqual(c['insert_entry'], bytes.fromhex('538bda578bf9'))
        self.assertEqual(c['remove_entry'], bytes.fromhex('56578bf98bf2'))
        self.assertEqual(c['destroy_entry'], bytes.fromhex('538bd9833b00'))
        self.assertEqual(d['destroy_continuation'][:1], b'\x74')  # JE: the replayed CMP's ZF is live
        self.assertEqual((c['load_anchor_rva'] + c['load_site_offset'], c['load_target_rva']), (0x7984c, 0x93510))
        self.assertEqual(c['load_anchor'][3:8], bytes.fromhex('e8bf9c0100'))
        self.assertEqual((c['engine_slot_rva'], c['registry_offset'], c['node_handle']), (0x1aa100, 0xc, 0x28))
        # One stack word read by insert (the value at entry [esp+4], read at [esp+0x10] after three pushes), none by
        # remove/destroy; every return a plain RET.
        self.assertEqual(len(d['insert_stack_reads']), 2)
        self.assertEqual((d['remove_stack_reads'], d['destroy_stack_reads']), ([], []))
        self.assertEqual((len(d['insert_returns']), len(d['remove_returns']), len(d['destroy_returns'])), (3, 2, 1))
        self.assertEqual(d['callers'], {'insert': 42, 'remove': 37, 'destroy': 33, 'load_target': 1})
        # Entries at qword offset 0 (one lock cmpxchg8b each); the load call at offset 4 crosses a qword (plain write).
        for rva in (c['insert_rva'], c['remove_rva'], c['destroy_rva']):
            self.assertEqual(rva % 8, 0)
        self.assertEqual(d['load_site_qword_offset'], 4)

    def test_gate_mirror_accepts_mapped_images_and_refuses_flips(self):
        data = need(FL_EXE)
        fields = gen.identity.parse(data)
        c = constants()
        for base in (fields['image_base'], *LIVE_BASES, 0x10000000):
            read = trace_test.mapped(data, fields, base)
            self.assertIsNone(gate(read, base, c), hex(base))
            self.assertEqual(gate(lambda a, n, r=read: r(a - 0x10000, n), base + 0x10000, c), 'engine_anchor_mismatch')
        base = LIVE_BASES[1]
        read = trace_test.mapped(data, fields, base)
        spans = [(c[f'{n}_rva'], 6, None) for n in ('insert', 'remove', 'destroy')]
        spans += [(c['load_anchor_rva'], len(c['load_anchor']), None), (c['load_target_rva'], 6, None)]
        spans += [(rva, len(masked), operand) for rva, masked, operand in c['anchors'].values()]
        flips = 0
        for rva, length, operand in spans:
            for i in range(length):
                if operand not in (None, 0xff) and operand <= i < operand + 4:
                    continue
                def flipped(address, size, at=base + rva + i):
                    span = bytearray(read(address, size))
                    if address <= at < address + size:
                        span[at - address] ^= 0x01
                    return bytes(span)
                self.assertIsNotNone(gate(flipped, base, c), (hex(rva), i))
                flips += 1
        self.assertEqual(flips, 84)

    def test_generator_refuses_mutations(self):
        data = need(FL_EXE)
        fields = gen.identity.parse(data)
        off = lambda rva: gen.identity.rva_to_offset(fields, rva)
        cases = {
            'ret 4 in insert': (0x10cfd2, b'\xc2\x04\x00'),       # a callee-cleaned return
            'stack read in remove': (0x10d09f, b'\x8b\x44\x24\x04\x90\x90\x90'),  # mov eax,[esp+4]; nop x3
            'branch into destroy entry': (0x10d126, b'\x74\xf9'),   # je 0x50d121
            'destroy without je': (0x10d126, b'\x75'),              # jne
            'remove anchor to another helper': (0x0a48a8, b'\x95'),
            'engine operand': (0x0a489d, b'\x04\xa1\x5a\x00'),
        }
        for name, (rva, patch) in cases.items():
            mutated = bytearray(data)
            mutated[off(rva):off(rva) + len(patch)] = patch
            with self.assertRaises(ValueError, msg=name):
                gen.decode(bytes(mutated), fields)


def build_and_run(tmp):
    """Builds the fixture from the production sources with the CMake per-file flags, runs it natively and audits
    its dispatch path. Returns (run, audit); None when the toolchain or interop is absent.
    Also used by verification/results/fl-object-lifetime/run_fixture.py."""
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
        return None
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
              '-mincoming-stack-boundary=2', '-DX3M_EDITION_FL=1', '-DX3M_OBJECT_LIFETIME_FIXTURE']
    light = ['-mno-sse', '-mno-mmx', '-mfpmath=387']
    sse = ['-msse2', '-mfpmath=sse']
    objects = []
    for source, flags in (('src/engine/fl/object_lifetime_fl.cpp', ['-fno-exceptions', *light]),
                          ('src/proxy/engine_memory.cpp', light), ('src/proxy/engine_patch.cpp', sse),
                          ('src/engine/fl/executable_identity_fl.cpp', sse)):
        obj = Path(tmp) / (Path(source).stem + '.o')
        subprocess.run([compiler, *common, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = Path(tmp) / 'fl_object_lifetime.exe'
    subprocess.run([compiler, *common, *sse, '-static', str(FIXTURE), *objects, '-o', str(exe)], check=True)
    run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=180)
    audit = subprocess.run(['python3', str(AUDIT), str(exe)], text=True, capture_output=True)
    return run, audit


class FlLifetimeNativeFixture(unittest.TestCase):
    """The production object_lifetime_fl.cpp, engine_memory.cpp, engine_patch.cpp and executable_identity_fl.cpp,
    linked into the fixture and run natively (WSL interop)."""

    def test_dispatcher_abi_model_rows_rollback_and_late_window(self):
        with tempfile.TemporaryDirectory(prefix='x3-fl-lifetime-') as tmp:
            outcome = build_and_run(tmp)
        if outcome is None:
            self.skipTest('needs i686-w64-mingw32-g++ and WSL interop (a native Windows run)')
        result, audit = outcome
        out = result.stdout
        self.assertEqual(result.returncode, 0, out[-4000:] + result.stderr[-1000:])
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0 target_calls=\d+')
        self.assertNotIn('FAIL ', out)
        self.assertRegex(out, r'TIMING native_ns=[\d.]+ hooked_ns=[\d.]+ hook_ns=-?[\d.]+ current_ns=[\d.]+')
        self.assertIn('ANCHOR flips=84 refused=84', out)
        self.assertRegex(out, r'REBASELINE entries=20002 load_us=\d+ rebaseline_us_max=\d+')
        self.assertEqual(len(re.findall(r'^LOG fl\.lifetime_sample ', out, re.M)), 2)
        self.assertEqual(audit.returncode, 0, audit.stdout[-2000:] + audit.stderr[-1000:])


class FlLifetimeWiring(unittest.TestCase):
    def test_edition_lists_loader_and_capture(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        block = lambda name: re.search(rf'set\({name}\n(.*?)\)\n', cmake, re.S).group(1)
        self.assertNotIn('object_lifetime', block('X3M_AP_ENGINE_LINKED_SOURCES'))
        self.assertNotIn('object_lifetime', block('X3M_SHARED_SOURCES'))
        self.assertIn('src/proxy/object_lifetime.cpp', block('X3M_AP_SOURCES'))
        self.assertIn('src/engine/fl/object_trace_fl.cpp\n    src/engine/fl/object_lifetime_fl.cpp)', cmake)
        self.assertIn('set_source_files_properties(src/engine/fl/object_lifetime_fl.cpp\n'
                      '  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-mno-sse;-mno-mmx;-mfpmath=387")', cmake)
        loader = (ROOT / 'src/proxy/loader.cpp').read_text()
        fl_block = loader[loader.index('#if X3M_EDITION_FL\n    // FL edition'):]
        fl_block = re.sub(r'//.*', '', fl_block[:fl_block.index('#endif')])
        self.assertLess(fl_block.index('x3m::object_trace::initialize();'),
                        fl_block.index('x3m::object_lifetime::initialize();'))
        self.assertIn('const bool traced_writes = x3m::object_trace::active() && x3m::object_lifetime::active();', loader)
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertRegex(capture, r'#if X3M_EDITION_FL\n(?:    //.*\n)*    object_lifetime::fl_sample_row\(\);\n#endif')
        fade = (ROOT / 'src/proxy/fade_region.cpp').read_text()
        self.assertIn('#if X3M_EDITION_FL\n    // The bound reads the descriptor with AP\'s layout', fade)  # still fenced

    def test_provider_contract(self):
        code = re.sub(r'//.*', '', PROVIDER.read_text())
        self.assertNotIn('0x400000', code)
        for va in ('0x50cf30', '0x50d040', '0x50d120', '0x47984c', '0x493510', '0x5aa100'):
            self.assertNotIn(va, code.lower())
        self.assertIn('x3m::fl::module_base()', code)
        self.assertIn('#include "object_lifetime_fl_inc.h"', code)
        for api in ('engine_patch::claim(', 'engine_patch::push_front(', 'engine_patch::claim_call(',
                    'engine_patch::install_window_open()', 'engine_patch::restore('):
            self.assertIn(api, code)
        for status in ('"disabled"', '"executable_mismatch"', '"late_claim"', '"active"', '"active_without_baseline"',
                       '"engine_anchor_mismatch"', '"insert_entry_mismatch"', '"load_site_mismatch"',
                       '"rollback_incomplete"', '"capacity_exhausted"'):
            self.assertIn(status, code)
        self.assertIn('"fl.lifetime status=%s', code)
        self.assertIn('"fl.lifetime_sample row=%lu', code)
        for banned in ('new ', 'malloc', 'std::string', 'float', 'double'):  # no allocation, no floating point
            self.assertIsNone(re.search(rf'\b{banned}', code), banned)
        # One stack word forwarded and dropped on return; the incoming EFLAGS restored before the call.
        self.assertIn('pushl 12(%eax)', code)
        self.assertIn('pushl 32(%ebp)\\n\\tpopfl', code)
        self.assertIn('movl 4(%esp),%esp\\n\\tleal 40(%esp),%esp\\n\\tret', code)


if __name__ == '__main__':
    unittest.main()
