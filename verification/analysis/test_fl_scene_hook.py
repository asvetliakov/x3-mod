"""Host tests of the FL scene-end hook (src/engine/fl/scene_hook_fl.cpp) and FL compositor owner read
(src/engine/fl/compositor_owner_fl.cpp).

docs/architecture/farnhams-legacy-scene.md. (a) The generated fragment (scene_hook_fl_inc.h) equals the
generator's output for X3FL.exe on disk: the 24-byte boundary window decodes as cmp [esi+0x29c],0x11; jle; call
<compositor>; done-byte store; flags reload, the call is the compositor's only direct caller, the compositor has no
stack argument and only bare RETs, no relocation touches window or entry, and the glow/owner/device anchors name
config +0xf0 bit 0x80 and renderer -> +0x18 -> [0]. A Python mirror of the DLL's gate accepts the image mapped at
FL run 1's base and refuses a flipped byte. (b) The native fixture verification/probe/fl_scene_hook.cpp (MinGW
i686 plus the production compositor bridge, run natively through WSL interop) drives both install paths through a
fake complete pass with the boundary taken both ways and checks registers, EFLAGS, XMM/x87/MXCSR, LastError,
nesting, foreign SEH unwind, the fl.scene_sample frame accounting, the owner/glow reads on a synthetic image,
restore and late-window refusal; the signal path is audited for x87 and XMM use. (c) Edition wiring. X3FL.exe is
only read.
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
FRAGMENT = ROOT / 'src/engine/fl/scene_hook_fl_inc.h'
PROVIDER = ROOT / 'src/engine/fl/scene_hook_fl.cpp'
OWNER = ROOT / 'src/engine/fl/compositor_owner_fl.cpp'
FIXTURE = ROOT / 'verification/probe/fl_scene_hook.cpp'
AUDIT = ROOT / 'verification/results/fl-scene-hook/hook_cpu_state_audit.py'
LIVE_BASE = 0x4d0000  # FL run 1 (docs/verification/farnhams-legacy.md)

spec = importlib.util.spec_from_file_location('fl_scene_hook_constants', ROOT / 'tools/analysis/fl_scene_hook_constants.py')
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
    out = {k: num(k) for k in ('site_rva', 'target_rva', 'window_rva', 'window_site_offset', 'glow_anchor_rva',
                               'glow_operand_offset', 'config_slot_rva', 'glow_flag_offset', 'glow_flag_mask',
                               'owner_anchor_rva', 'owner_operand_offset', 'renderer_slot_rva', 'record_offset',
                               'device_anchor_rva', 'device_offset')}
    out.update({k: arr(k) for k in ('window', 'target_entry', 'glow_anchor', 'owner_anchor', 'device_anchor')})
    return out


def slot_anchor(read, base, rva, expected, operand, slot):
    actual = read(base + rva, len(expected))
    if actual is None or struct.unpack_from('<I', actual, operand)[0] != (base + slot) & 0xffffffff:
        return False
    masked = bytearray(actual)
    masked[operand:operand + 4] = b'\0\0\0\0'
    return bytes(masked) == expected


def gate(read, base, c):
    """Python mirror of scene_hook_fl.cpp initialize()'s byte gates at `base`: None or the refusal."""
    window, target = base + c['window_rva'], base + c['target_rva']
    call = c['window_site_offset']
    code = read(window, len(c['window']))
    if code is None or code[call] != 0xe8:
        return 'callsite_mismatch'
    if (window + call + 5 + struct.unpack_from('<I', code, call + 1)[0]) & 0xffffffff != target:
        return 'target_mismatch'
    expected = bytearray(c['window'])
    expected[call + 1:call + 5] = code[call + 1:call + 5]
    if code != bytes(expected):
        return 'boundary_mismatch'
    if read(target, len(c['target_entry'])) != c['target_entry']:
        return 'entry_mismatch'
    if not slot_anchor(read, base, c['glow_anchor_rva'], c['glow_anchor'], c['glow_operand_offset'], c['config_slot_rva']):
        return 'glow_anchor_mismatch'
    if not slot_anchor(read, base, c['owner_anchor_rva'], c['owner_anchor'], c['owner_operand_offset'],
                       c['renderer_slot_rva']) or read(base + c['device_anchor_rva'], 4) != c['device_anchor']:
        return 'owner_anchor_mismatch'
    return None


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
                o = gen.identity.rva_to_offset(fields, r)
                word = struct.pack('<I', (struct.unpack_from('<I', data, o)[0] + delta) & 0xffffffff)
                for k in range(4):
                    if 0 <= r + k - rva < size:
                        span[r + k - rva] = word[k]
        return bytes(span)
    return read


class FlSceneConstants(unittest.TestCase):
    def test_fragment_is_generated_from_x3fl(self):
        data = need(FL_EXE)
        self.assertEqual(FRAGMENT.read_text(), gen.fragment(data, gen.identity.parse(data)))

    def test_documented_site_abi_and_owner_layout(self):
        data = need(FL_EXE)
        d = gen.decode(data, gen.identity.parse(data))
        c = constants()
        self.assertEqual((c['window_rva'], c['site_rva'], c['target_rva'], c['window_site_offset']),
                         (0x8b920, 0x8b929, 0xe6770, 9))
        # The brief's fl-codex check window, byte for byte (rel32 0x5ae42 == 0xe6770 - 0x8b92e).
        self.assertEqual(c['window'], bytes.fromhex('83be9c020000117e09e842ae0500c645f3018b8e70020000'))
        self.assertEqual(c['target_entry'], bytes.fromhex('558d6c24c864a10000000083ec38'))
        self.assertEqual(d['target_callers'], [0x8b929])
        self.assertEqual(d['pass_callers'], [0x782cf])
        self.assertEqual(d['target_instructions'], 553)
        self.assertEqual(d['target_rets'], [0xe6887, 0xe69b3, 0xe6e86])
        # Both branches into the window land on the flags reload after the done-byte store.
        self.assertEqual([t for _, t in d['window_branches']], [0x8b932, 0x8b932])
        self.assertEqual((c['config_slot_rva'], c['glow_flag_offset'], c['glow_flag_mask']), (0x1a83b0, 0xf0, 0x80))
        self.assertEqual((c['renderer_slot_rva'], c['record_offset'], c['device_offset']), (0x1aaf94, 0x18, 0))
        # The call is one byte into its qword (an RVA keeps that at a 64K-aligned base): one lock cmpxchg8b write.
        self.assertLessEqual(c['site_rva'] % 8 + 5, 8)

    def test_gate_mirror_accepts_the_mapped_image_and_refuses_mutations(self):
        data = need(FL_EXE)
        fields = gen.identity.parse(data)
        c = constants()
        for base in (fields['image_base'], LIVE_BASE, 0x10000000):
            read = mapped(data, fields, base)
            self.assertIsNone(gate(read, base, c), hex(base))
            self.assertEqual(gate(lambda a, n, r=read: r(a - 0x10000, n), base + 0x10000, c), 'glow_anchor_mismatch')
        read = mapped(data, fields, LIVE_BASE)
        spans = {'window': (c['window_rva'], len(c['window'])), 'entry': (c['target_rva'], len(c['target_entry'])),
                 'glow': (c['glow_anchor_rva'], len(c['glow_anchor'])),
                 'owner': (c['owner_anchor_rva'], len(c['owner_anchor'])),
                 'device': (c['device_anchor_rva'], len(c['device_anchor']))}
        for name, (rva, length) in spans.items():
            for i in range(length):
                start = LIVE_BASE + rva

                def flipped(address, size, start=start, i=i):
                    span = bytearray(read(address, size))
                    if address <= start + i < address + size:
                        span[start + i - address] ^= 0x01
                    return bytes(span)
                self.assertIsNotNone(gate(flipped, LIVE_BASE, c), (name, i))
        self.assertEqual(gate(lambda a, n: None, LIVE_BASE, c), 'callsite_mismatch')

    def test_generator_refuses_a_moved_threshold_a_stack_argument_and_ap_glow(self):
        data = need(FL_EXE)
        fields = gen.identity.parse(data)
        for rva, value in ((0x8b926, 0x12),        # layer threshold 18
                           (0x8b928, 0x0a),        # AP's jle distance
                           (0xe67f7, 0x40),        # mov [ebp+0x10],ebx -> [ebp+0x40]: a caller stack word
                           (0xe6797, 0x00)):       # test [eax+0xf0] -> [eax+0x0]: another glow byte
            mutated = bytearray(data)
            mutated[gen.identity.rva_to_offset(fields, rva)] = value
            with self.assertRaises(ValueError, msg=hex(rva)):
                gen.decode(bytes(mutated), fields)


def build_and_run(tmp):
    """Builds the fixture from the production sources (CMake per-file flags) plus the production compositor bridge,
    runs it natively and audits its signal path. Returns (run, audit) CompletedProcess objects; None when the
    toolchain or interop is absent. Also used by verification/results/fl-scene-hook/run_fixture.py."""
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler or not shutil.which('clang') or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
        return None
    tmp = Path(tmp)
    subprocess.run(['python3', str(ROOT / 'tools/build/build_compositor_bridge.py'), '--output-dir', str(tmp / 'bridge'),
                    '--gnu-pe-no-safeseh'], check=True, capture_output=True)
    bridge_asm = tmp / 'compositor_bridge.o'
    subprocess.run(['i686-w64-mingw32-gcc', '-c', '-msse2', '-mfpmath=sse', '-mstackrealign',
                    '-mincoming-stack-boundary=2', str(ROOT / 'src/proxy/compositor_bridge.S'), '-o', str(bridge_asm)],
                   check=True)
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
              '-mincoming-stack-boundary=2', '-DX3M_EDITION_FL=1', '-DX3M_SCENE_HOOK_FIXTURE']
    light = ['-mno-sse', '-mno-mmx', '-mfpmath=387']
    sse = ['-msse2', '-mfpmath=sse']
    objects = []
    for source, flags in (('src/engine/fl/scene_hook_fl.cpp', ['-fno-exceptions', *light]),
                          ('src/engine/fl/compositor_owner_fl.cpp', sse), ('src/proxy/engine_patch.cpp', sse),
                          ('src/engine/fl/executable_identity_fl.cpp', sse)):
        obj = tmp / (Path(source).stem + '.o')
        subprocess.run([compiler, *common, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = tmp / 'fl_scene_hook.exe'
    subprocess.run([compiler, *common, *sse, '-static', str(FIXTURE), *objects, str(bridge_asm),
                    str(tmp / 'bridge/compositor_bridge_seh_gnu.obj'),
                    str(tmp / 'bridge/libx3m_compositor_seh_runtime.a'), '-o', str(exe)], check=True)
    run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=120)
    audit = subprocess.run(['python3', str(AUDIT), str(exe)], text=True, capture_output=True)
    return run, audit


class FlSceneNativeFixture(unittest.TestCase):
    """The production scene_hook_fl.cpp, compositor_owner_fl.cpp, engine_patch.cpp, executable_identity_fl.cpp and
    compositor bridge, linked into the fixture and run natively (WSL interop)."""

    def test_both_paths_boundary_cpu_state_unwind_rows_owner_and_late_window(self):
        with tempfile.TemporaryDirectory(prefix='x3-fl-scene-') as tmp:
            outcome = build_and_run(tmp)
        if outcome is None:
            self.skipTest('needs i686-w64-mingw32-g++, clang and WSL interop (a native Windows run)')
        result, audit = outcome
        out = result.stdout
        self.assertEqual(result.returncode, 0, out[-4000:] + result.stderr[-1000:])
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0 target_calls=\d+')
        self.assertNotIn('FAIL ', out)
        self.assertRegex(out, r'TIMING native_ns=[\d.]+ signal_ns=[\d.]+ bridge_ns=[\d.]+')
        self.assertEqual(audit.returncode, 0, audit.stdout[-2000:] + audit.stderr[-1000:])
        # The production C++ gate against a synthetic image: 61 single-byte flips, all refused.
        self.assertIn('ANCHOR flips=61 refused=61', out)
        self.assertEqual(len(re.findall(r'^LOG fl\.scene_sample ', out, re.M)), 2)


class FlSceneWiring(unittest.TestCase):
    def test_edition_lists_loader_and_present(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        linked = re.search(r'set\(X3M_AP_ENGINE_LINKED_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        shared = re.search(r'set\(X3M_SHARED_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        ap = re.search(r'set\(X3M_AP_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        for source in ('scene_hook.cpp', 'compositor_owner.cpp'):
            self.assertNotIn(source, linked + shared)
            self.assertIn('src/proxy/' + source, ap)
        self.assertIn('src/engine/fl/scene_hook_fl.cpp src/engine/fl/compositor_owner_fl.cpp)', cmake)
        self.assertIn('set_source_files_properties(src/engine/fl/scene_hook_fl.cpp\n'
                      '  PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-mno-sse;-mno-mmx;-mfpmath=387")', cmake)
        loader = (ROOT / 'src/proxy/loader.cpp').read_text()
        fl_block = loader[loader.index('#if X3M_EDITION_FL\n    // FL edition'):]
        fl_block = re.sub(r'//.*', '', fl_block[:fl_block.index('#endif')])
        self.assertIn('x3m::scene_hook::initialize(&x3m::scene_end_signal, x3m::compositor_binding());', fl_block)
        self.assertLess(fl_block.index('object_lifetime::initialize'), fl_block.index('scene_hook::initialize'))
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        present = capture[capture.index('    object_lifetime::fl_sample_row();'):]
        self.assertIn('scene_hook::fl_sample_row();', present[:present.index('#endif')])
        glow = capture[capture.index('bool compositor_glow_enabled('):]
        self.assertIn('#if X3M_EDITION_FL\n', glow[:glow.index('0x206f34')])

    def test_provider_contract(self):
        for path in (PROVIDER, OWNER):
            code = re.sub(r'//.*', '', path.read_text())
            self.assertNotIn('0x400000', code)
            for va in ('0x48b929', '0x4e6770', '0x5aaf94', '0x5a83b0', '0x208b3c', '0x206f34'):
                self.assertNotIn(va, code.lower())
        code = re.sub(r'//.*', '', PROVIDER.read_text())
        self.assertIn('x3m::fl::module_base()', code)
        self.assertIn('#include "scene_hook_fl_inc.h"', code)
        self.assertIn('engine_patch::claim_call', code)
        self.assertIn('engine_patch::install_window_open()', code)
        self.assertIn('x3m::compositor_owner::admit(base)', code)
        for status in ('"disabled"', '"executable_mismatch"', '"late_claim"', '"active"', '"callsite_mismatch"',
                       '"target_mismatch"', '"boundary_mismatch"', '"entry_mismatch"', '"glow_anchor_mismatch"',
                       '"owner_anchor_mismatch"', '"bridge_bind_failed"'):
            self.assertIn(status, code)
        self.assertIn('"fl.scene status=%s', code)
        self.assertIn('"fl.scene_sample row=%lu', code)
        for banned in ('new ', 'malloc', 'std::string', 'float', 'double', 'atomic<unsigned long long>'):
            self.assertNotIn(banned, code)
        self.assertIn('"pushfl\\n\\tpushal\\n\\tcld\\n\\t"', code)
        owner = re.sub(r'//.*', '', OWNER.read_text())
        self.assertIn('#include "scene_hook_fl_inc.h"', owner)
        self.assertIn('layout::renderer_slot_rva', owner)
        self.assertIn('layout::glow_flag_offset', owner)


if __name__ == '__main__':
    unittest.main()
