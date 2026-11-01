"""Host tests of the FL material-submission scope hook (src/engine/fl/object_trace_fl.cpp).

docs/architecture/farnhams-legacy-object-identity.md, "Step 3a: scope hook". (a) The generated fragment
(object_trace_fl_inc.h) equals the generator's output for X3FL.exe on disk: the site is `call rel32` to the
material target followed by the caller's `add esp,0x10`, the target begins `push ebx; mov ebx,esp; sub esp,8`,
neither is touched by a base relocation, the site is the target's only direct caller, the engine anchor's
disp32 is its one relocated word, and every node/camera offset comes from a native operand (model +0x144,
LOD +0x150). A Python mirror of the DLL's gate accepts the image mapped at FL run 1's base and refuses a
flipped byte. (b) The native fixture verification/probe/fl_object_trace.cpp (MinGW i686, run natively
through WSL interop) drives the real dispatcher through a fake caller with the FL ABI (ECX/EDX, four pushed
words, caller cleanup) and checks registers, XMM/x87/MXCSR, LastError, nesting, foreign SEH unwind, TLS
failures, the fl.trace_sample row, restore and late-window refusal; its dispatch path is audited for x87 and
XMM use. (c) Edition wiring. X3FL.exe is only read.
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
FRAGMENT = ROOT / 'src/engine/fl/object_trace_fl_inc.h'
PROVIDER = ROOT / 'src/engine/fl/object_trace_fl.cpp'
FIXTURE = ROOT / 'verification/probe/fl_object_trace.cpp'
AUDIT = ROOT / 'verification/results/fl-object-trace/hook_cpu_state_audit.py'
LIVE_BASE = 0x4d0000  # FL run 1 (docs/verification/farnhams-legacy.md)

spec = importlib.util.spec_from_file_location('fl_object_trace_constants', ROOT / 'tools/analysis/fl_object_trace_constants.py')
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)


def need(path):
    if not path.exists():
        raise unittest.SkipTest(f'{path} absent')
    return path.read_bytes()


def constants():
    """The values of the generated C++ fragment."""
    text = FRAGMENT.read_text()
    num = lambda name: int(re.search(rf'\b{name} = (0x[0-9a-f]+|\d+)', text).group(1), 0)
    arr = lambda name: bytes(int(b, 16) for b in re.search(rf'{name}\[\d+\] = \{{([^}}]*)\}}', text).group(1).split(','))
    out = {k: num(k) for k in ('site_rva', 'target_rva', 'engine_anchor_rva', 'engine_slot_rva', 'engine_operand_offset',
                               'registry_offset')}
    out.update(site_bytes=arr('site_bytes'), target_entry=arr('target_entry'), engine_anchor=arr('engine_anchor'))
    out['offsets'] = {m.group(1): int(m.group(2), 16) for m in re.finditer(r'constexpr std::uint32_t (\w+) = (0x[0-9a-f]+); //', text)}
    return out


def gate(read, base, c):
    """Python mirror of object_trace_fl.cpp initialize()'s byte gates at `base`: None or the refusal."""
    site, target = base + c['site_rva'], base + c['target_rva']
    code = read(site, 8)
    if code is None or code[0] != 0xe8:
        return 'site_mismatch'
    if (site + 5 + struct.unpack_from('<I', code, 1)[0]) & 0xffffffff != target:
        return 'target_mismatch'
    if code[5:8] != c['site_bytes'][5:8]:
        return 'cleanup_mismatch'
    if code != c['site_bytes']:
        return 'site_mismatch'
    if read(target, 6) != c['target_entry']:
        return 'entry_mismatch'
    anchor = read(base + c['engine_anchor_rva'], len(c['engine_anchor']))
    if anchor is None:
        return 'engine_anchor_mismatch'
    off = c['engine_operand_offset']
    if struct.unpack_from('<I', anchor, off)[0] != (base + c['engine_slot_rva']) & 0xffffffff:
        return 'engine_anchor_mismatch'
    masked = bytearray(anchor)
    masked[off:off + 4] = b'\0\0\0\0'
    return None if bytes(masked) == c['engine_anchor'] else 'engine_anchor_mismatch'


def mapped(data, fields, base):
    """read(address, size) over the file as the loader maps it at `base` (HIGHLOW relocations applied)."""
    relocated = set(gen.identity.relocated_rvas(data, fields))
    delta = base - fields['image_base']

    def read(address, size):
        rva = address - base
        try:
            off = gen.identity.rva_to_offset(fields, rva)
        except ValueError:
            return None
        span = bytearray(data[off:off + size])
        for r in relocated:
            if rva - 3 <= r < rva + size:
                word = bytearray(data[gen.identity.rva_to_offset(fields, r):gen.identity.rva_to_offset(fields, r) + 4])
                struct.pack_into('<I', word, 0, (struct.unpack('<I', word)[0] + delta) & 0xffffffff)
                for k in range(4):
                    if 0 <= r + k - rva < size:
                        span[r + k - rva] = word[k]
        return bytes(span)
    return read


class FlTraceConstants(unittest.TestCase):
    def test_fragment_is_generated_from_x3fl(self):
        data = need(FL_EXE)
        self.assertEqual(FRAGMENT.read_text(), gen.fragment(data, gen.identity.parse(data)))

    def test_documented_site_abi_and_layout(self):
        data = need(FL_EXE)
        d = gen.decode(data, gen.identity.parse(data))
        c = constants()
        self.assertEqual((c['site_rva'], c['target_rva']), (0xe7142, 0xe0f30))
        self.assertEqual(c['site_bytes'], bytes.fromhex('e8e99dffff83c410'))  # rel32 == target - (site + 5); caller cleanup
        self.assertEqual(c['target_entry'], bytes.fromhex('538bdc83ec08'))
        self.assertEqual((c['engine_slot_rva'], c['registry_offset']), (0x1aa100, 0xc))
        self.assertEqual(d['target_callers'], [0xe7142])
        self.assertEqual(d['wrapper_callers'], [0x97035, 0x97702])
        self.assertEqual(c['offsets'], {'node_parent': 0x18, 'node_handle': 0x28, 'camera_handle': 0x28, 'node_scale': 0x70,
                                        'node_scale_axes': 0x80, 'node_position': 0xb0, 'node_basis': 0xc0,
                                        'node_flags12c': 0x12c, 'node_flags130': 0x130, 'node_model': 0x144,
                                        'node_lod': 0x150})
        # The site is 2 bytes into its qword (an RVA keeps that at a 64K-aligned base): one lock cmpxchg8b write.
        self.assertLessEqual(c['site_rva'] % 8 + 5, 8)

    def test_gate_mirror_accepts_the_mapped_image_and_refuses_mutations(self):
        data = need(FL_EXE)
        fields = gen.identity.parse(data)
        c = constants()
        for base in (fields['image_base'], LIVE_BASE, 0x10000000):
            read = mapped(data, fields, base)
            self.assertIsNone(gate(read, base, c), hex(base))
            # The image mapped for one base, checked against another: the engine operand no longer matches.
            self.assertEqual(gate(lambda a, n, r=read: r(a - 0x10000, n), base + 0x10000, c), 'engine_anchor_mismatch')
        read = mapped(data, fields, LIVE_BASE)
        spans = {'site': (LIVE_BASE + c['site_rva'], 8), 'entry': (LIVE_BASE + c['target_rva'], 6),
                 'anchor': (LIVE_BASE + c['engine_anchor_rva'], len(c['engine_anchor']))}
        for name, (start, length) in spans.items():
            for i in range(length):
                def flipped(address, size, start=start, i=i):
                    span = bytearray(read(address, size))
                    if address <= start + i < address + size:
                        span[start + i - address] ^= 0x01
                    return bytes(span)
                self.assertIsNotNone(gate(flipped, LIVE_BASE, c), (name, i))
        self.assertEqual(gate(lambda a, n: None, LIVE_BASE, c), 'site_mismatch')

    def test_generator_refuses_ap_offsets_and_a_moved_cleanup(self):
        data = need(FL_EXE)
        fields = gen.identity.parse(data)
        model = bytearray(data)
        off = gen.identity.rva_to_offset(fields, 0x968e1 + 2)
        struct.pack_into('<I', model, off, 0x140)  # AP's model offset in the traversal load
        with self.assertRaises(ValueError):
            gen.decode(bytes(model), fields)
        cleanup = bytearray(data)
        cleanup[gen.identity.rva_to_offset(fields, 0xe7147 + 2)] = 0x0c
        with self.assertRaises(ValueError):
            gen.decode(bytes(cleanup), fields)


def build_and_run(tmp):
    """Builds the fixture from the production sources with the CMake per-file flags, runs it natively and audits
    its dispatch path. Returns (run, audit) CompletedProcess objects; None when the toolchain or interop is absent.
    Also used by verification/results/fl-object-trace/run_fixture.py."""
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
        return None
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
              '-mincoming-stack-boundary=2', '-DX3M_EDITION_FL=1', '-DX3M_OBJECT_TRACE_FIXTURE']
    light = ['-mno-sse', '-mno-mmx', '-mfpmath=387']
    sse = ['-msse2', '-mfpmath=sse']
    objects = []
    for source, flags in (('src/engine/fl/object_trace_fl.cpp', ['-fno-exceptions', *light]),
                          ('src/proxy/engine_memory.cpp', light), ('src/proxy/engine_patch.cpp', sse),
                          ('src/engine/fl/executable_identity_fl.cpp', sse)):
        obj = Path(tmp) / (Path(source).stem + '.o')
        subprocess.run([compiler, *common, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = Path(tmp) / 'fl_object_trace.exe'
    subprocess.run([compiler, *common, *sse, '-static', str(FIXTURE), *objects, '-o', str(exe)], check=True)
    run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=120)
    audit = subprocess.run(['python3', str(AUDIT), str(exe)], text=True, capture_output=True)
    return run, audit


class FlTraceNativeFixture(unittest.TestCase):
    """The production object_trace_fl.cpp, engine_memory.cpp, engine_patch.cpp and executable_identity_fl.cpp,
    linked into the fixture and run natively (WSL interop)."""

    def test_dispatcher_abi_unwind_rows_restore_and_late_window(self):
        with tempfile.TemporaryDirectory(prefix='x3-fl-trace-') as tmp:
            outcome = build_and_run(tmp)
        if outcome is None:
            self.skipTest('needs i686-w64-mingw32-g++ and WSL interop (a native Windows run)')
        result, audit = outcome
        out = result.stdout
        self.assertEqual(result.returncode, 0, out[-4000:] + result.stderr[-1000:])
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0 target_calls=\d+')
        self.assertNotIn('FAIL ', out)
        self.assertRegex(out, r'TIMING native_ns=[\d.]+ hooked_ns=[\d.]+ hooked_current_ns=[\d.]+')
        self.assertEqual(audit.returncode, 0, audit.stdout[-2000:] + audit.stderr[-1000:])
        # The production C++ gate against a synthetic image: 21 single-byte flips, all refused.
        self.assertIn('ANCHOR flips=21 refused=21', out)
        self.assertEqual(len(re.findall(r'^LOG fl\.trace_sample ', out, re.M)), 1)
        # Why the dispatcher's rare paths save XMM0-7: a VirtualQuery returns with some changed (natively:
        # XMM0-5 zero, XMM6-7 kept, measured 2026-11-01). Recorded; at least one changed register required.
        changed = re.search(r'^XMM_AFTER_VIRTUALQUERY changed=(\d) zero=(\d) of=8$', out, re.M)
        self.assertIsNotNone(changed)
        self.assertGreaterEqual(int(changed.group(1)), 1)


class FlTraceWiring(unittest.TestCase):
    def test_edition_lists_and_loader(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        linked = re.search(r'set\(X3M_AP_ENGINE_LINKED_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        self.assertNotIn('object_trace.cpp', linked)
        self.assertNotIn('object_trace', re.search(r'set\(X3M_SHARED_SOURCES\n(.*?)\)\n', cmake, re.S).group(1))
        self.assertIn('src/proxy/object_trace.cpp', re.search(r'set\(X3M_AP_SOURCES\n(.*?)\)\n', cmake, re.S).group(1))
        self.assertIn('src/engine/fl/camera_state_fl.cpp src/engine/fl/object_trace_fl.cpp\n', cmake)
        self.assertIn('set_source_files_properties(src/engine/fl/object_trace_fl.cpp\n'
                      '  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-mno-sse;-mno-mmx;-mfpmath=387")', cmake)
        loader = (ROOT / 'src/proxy/loader.cpp').read_text()
        fl_block = loader[loader.index('#if X3M_EDITION_FL\n    // FL edition'):]
        fl_block = fl_block[:fl_block.index('#endif')]
        self.assertIn('x3m::object_trace::initialize();', fl_block)
        # Step 3b initializes the registry observer after the scope hook (test_fl_object_lifetime.py).
        code = re.sub(r'//.*', '', fl_block)
        self.assertLess(code.index('object_trace::initialize'), code.index('object_lifetime::initialize'))

    def test_provider_contract(self):
        code = re.sub(r'//.*', '', PROVIDER.read_text())
        self.assertNotIn('0x400000', code)
        for va in ('0x4e7142', '0x4e0f30', '0x5aa100', '0x4a2290'):  # no preferred VAs; RVAs come from the fragment
            self.assertNotIn(va, code.lower())
        self.assertIn('x3m::fl::module_base()', code)
        self.assertIn('#include "object_trace_fl_inc.h"', code)
        self.assertIn('engine_patch::claim_call', code)
        self.assertIn('engine_patch::install_window_open()', code)
        for status in ('"disabled"', '"executable_mismatch"', '"engine_anchor_mismatch"', '"late_claim"', '"active"',
                       '"site_mismatch"', '"target_mismatch"', '"cleanup_mismatch"', '"entry_mismatch"'):
            self.assertIn(status, code)
        self.assertIn('"fl.trace status=%s', code)
        self.assertIn('"fl.trace_sample row=%lu', code)
        self.assertIn('QueryPerformanceCounter', code)
        for banned in ('new ', 'malloc', 'std::string', 'float', 'double'):
            self.assertNotIn(banned, code)
        # The dispatcher removes its own copy of the four words after the target's plain RET.
        self.assertIn('call *_x3m_fl_object_original\\n\\taddl $16,%esp', code)


if __name__ == '__main__':
    unittest.main()
