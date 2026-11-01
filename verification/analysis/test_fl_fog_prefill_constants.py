"""Host tests of the FL fog-prefill constants (src/engine/fl/fog_prefill_fl_inc.h).

docs/reverse-engineering/fl/fog-prefill.md, "Verified constants (2026-11-01)". The fragment is generated from
X3FL.exe on disk by tools/analysis/fl_fog_prefill_constants.py: every anchor is a run of whole instructions,
every base relocation in it names a documented slot, every field offset is read from a named native
operand, eax keeps the alive marker between its load and store, and the native command table routes cases
0, 0x128 and 0x133 to the anchored handlers. A Python mirror of the runtime anchor check accepts each span
relocated to the preferred base, FL run 1's live base 0x4d0000 and 0x10000000, and refuses a flipped byte, a
foreign base and an unreadable span. The generator refuses moved slots, changed offsets/markers, a clobbered
marker register, a rerouted command case, a moved frame call, a split instruction and the AP image. X3FL.exe
is only read.
"""
import importlib.util
from pathlib import Path
import re
import struct
import unittest

ROOT = Path(__file__).resolve().parents[2]
FL_EXE = Path('/mnt/c/X3FL/X3FL.exe')
AP_EXE = Path('/mnt/c/X3AP-orig/X3AP.exe')
FRAGMENT = ROOT / 'src/engine/fl/fog_prefill_fl_inc.h'
LIVE_BASE = 0x4d0000  # FL run 1 (docs/verification/farnhams-legacy.md)

spec = importlib.util.spec_from_file_location('fl_fog_prefill_constants',
                                              ROOT / 'tools/analysis/fl_fog_prefill_constants.py')
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)


def need(path):
    if not path.exists():
        raise unittest.SkipTest(f'{path} absent')
    return path.read_bytes()


def fragment_anchors():
    """The anchors of the generated C++ fragment, as the DLL sees them."""
    text = FRAGMENT.read_text()
    arrays = {m.group(1): bytes(int(b, 16) for b in m.group(3).split(','))
              for m in re.finditer(r'constexpr std::uint8_t (\w+)_bytes\[(\d+)\] = \{([^}]*)\};', text)}
    out = {}
    for m in re.finditer(r'\{"(\w+)", (0x[0-9a-f]+), (\d+), (\d+), \{([^}]*)\}, \{([^}]*)\}, (\w+)_bytes\}', text):
        n = int(m.group(4))
        out[m.group(1)] = {'rva': int(m.group(2), 16), 'length': int(m.group(3)),
                           'offsets': [int(x) for x in m.group(5).split(',')][:n],
                           'slots': [int(x, 16) for x in m.group(6).split(',')][:n], 'bytes': arrays[m.group(7)]}
    return out


def fragment_value(name):
    m = re.search(rf'constexpr std::uint32_t {name} = (0x[0-9a-f]+|\d+);', FRAGMENT.read_text())
    return int(m.group(1), 0)


def anchor_matches(image_read, base, a):
    """Python mirror of the runtime check: image_read(address, size) -> bytes or None."""
    actual = image_read(base + a['rva'], a['length'])
    if actual is None or len(actual) != a['length']:
        return False
    masked = bytearray(actual)
    for off, slot in zip(a['offsets'], a['slots']):
        if struct.unpack_from('<I', actual, off)[0] != (base + slot) & 0xffffffff:
            return False
        masked[off:off + 4] = b'\0\0\0\0'
    return bytes(masked) == a['bytes']


def relocated_span(data, fields, a, base):
    """The file's span with every HIGHLOW relocation in it applied for `base` (what the loader maps)."""
    off = gen.identity.rva_to_offset(fields, a['rva'])
    span = bytearray(data[off:off + a['length']])
    for r in gen.identity.relocated_rvas(data, fields):
        if a['rva'] <= r < a['rva'] + a['length']:
            value = struct.unpack_from('<I', span, r - a['rva'])[0] - fields['image_base'] + base
            struct.pack_into('<I', span, r - a['rva'], value & 0xffffffff)
    return bytes(span)


def patched(data, fields, rva, new):
    out = bytearray(data)
    off = gen.identity.rva_to_offset(fields, rva)
    out[off:off + len(new)] = new
    return bytes(out)


class FlFogPrefillConstants(unittest.TestCase):
    def test_fragment_is_generated_from_x3fl(self):
        data = need(FL_EXE)
        self.assertEqual(FRAGMENT.read_text(), gen.fragment(data, gen.identity.parse(data)))

    def test_documented_slots_and_offsets(self):
        need(FL_EXE)
        # AP fog_prefill.h: kManager 0x60850c, list M+8 / tailpred M+0x10, prev +4, type +0x48, marker +0x9c 0xcafe,
        # block +0x130 (index dword 3), id +8, count 0x607040, table 0x606fc0.
        expected = {'object_manager_slot_rva': 0x1a82ec, 'background_count_slot_rva': 0x1a8338,
                    'background_table_slot_rva': 0x1a83d0, 'list_header': 8, 'list_tail': 0xc, 'list_tailpred': 0x10,
                    'link_next': 0, 'link_prev': 4, 'object_id': 8, 'object_class': 0x48, 'object_subtype': 0x4a,
                    'object_parent': 0x54, 'object_marker': 0x9c, 'alive_marker': 0xcafe, 'dead_marker': 0xefac,
                    'sector_class': 1, 'sector_object_size': 0x180, 'sector_scene': 0x130,
                    'sector_background_index': 0x13c, 'record_stride': 0xdb8, 'task_runner_rva': 0x788b0,
                    'cockpit_update_rva': 0x1e550, 'render_pass_rva': 0x8b6d0, 'loop_head_rva': 0x77e80}
        for name, value in expected.items():
            self.assertEqual(fragment_value(name), value, name)
        self.assertEqual(fragment_value('list_header') + fragment_value('header_tailpred'), fragment_value('list_tailpred'))
        self.assertEqual(fragment_value('sector_background_index') - fragment_value('sector_scene'), 12)

    def test_every_relocation_is_a_named_slot(self):
        data = need(FL_EXE)
        fields = gen.identity.parse(data)
        relocated = gen.identity.relocated_rvas(data, fields)
        anchors = fragment_anchors()
        self.assertEqual(len(anchors), len(gen.ANCHORS))
        self.assertTrue(all(a['length'] <= 128 and len(a['offsets']) <= 3 for a in anchors.values()))
        runtime = {fragment_value(n) for n in gen.RUNTIME_SLOTS if n != 'background_count_slot_rva'}
        covered = set()
        for name, a in anchors.items():
            inside = sorted(r - a['rva'] for r in relocated if a['rva'] - 3 <= r < a['rva'] + a['length'])
            self.assertEqual(inside, a['offsets'], name)
            covered |= set(a['slots'])
        self.assertLessEqual(runtime, covered)  # the count slot is anchored by sector_background_fl_inc.h

    def test_in_memory_mirror_accepts_the_relocated_span_and_refuses_mutations(self):
        data = need(FL_EXE)
        fields = gen.identity.parse(data)
        for name, a in fragment_anchors().items():
            for base in (fields['image_base'], LIVE_BASE, 0x10000000):
                span = relocated_span(data, fields, a, base)
                read = lambda address, size, span=span, at=base + a['rva']: span if address == at and size == len(span) else None
                self.assertTrue(anchor_matches(read, base, a), (name, hex(base)))
                self.assertFalse(anchor_matches(read, base + 0x10000, a), (name, hex(base)))
                for i in range(a['length']):
                    flipped = bytearray(span)
                    flipped[i] ^= 0x01
                    read_flipped = lambda address, size, s=bytes(flipped): s if size == len(s) else None
                    self.assertFalse(anchor_matches(read_flipped, base, a), (name, hex(base), i))
                self.assertFalse(anchor_matches(lambda address, size: None, base, a))

    def test_generator_refuses_moved_slots_and_changed_operands(self):
        data = need(FL_EXE)
        fields = gen.identity.parse(data)
        image_base = fields['image_base']
        cases = (
            # the sector AddTail reading the cockpit registry slot instead of the manager
            (0x05c14d + 2, struct.pack('<I', image_base + 0x1a82e4)),
            # the manager published to the next slot
            (0x0778a9 + 1, struct.pack('<I', image_base + 0x1a82f0)),
            # header tailpred at header +0xc
            (0x03dc8e + 2, bytes([0x0c])),
            # node prev link at +8
            (0x05c158 + 2, bytes([0x08])),
            # the scene setter stores at +0x134
            (0x05c264 + 2, struct.pack('<I', 0x134)),
            # the background command stores the index at +0x140
            (0x06eac5 + 2, struct.pack('<I', 0x140)),
            # alive marker 0xcafd
            (0x049963 + 1, struct.pack('<I', 0xcafd)),
            # marker stored at +0x98
            (0x049a20 + 3, struct.pack('<I', 0x98)),
            # dead marker 0xefad
            (0x049619 + 1, struct.pack('<I', 0xefad)),
            # eax clobbered between the marker load and store: mov eax, 0 + five nops over a 10-byte store
            (0x049968, bytes([0xb8, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90, 0x90])),
            # SA_AllocObject's sector branch taken for class 2
            (0x06a6b1 + 2, bytes([0x02])),
            # native case 0x133 routed past the anchored handler
            (0x073dc4 + 4 * 0x133, struct.pack('<I', image_base + 0x06eadc)),
            # the render pass call retargeted
            (0x0782cf + 1, struct.pack('<i', 0x0133fc + 0x10)),
            # a split instruction: the first byte of the sector append becomes a one-byte opcode
            (0x05c148, bytes([0x90])),
        )
        for rva, new in cases:
            with self.assertRaises(ValueError, msg=hex(rva)):
                gen.decode(patched(data, fields, rva, new), fields)

    def test_generator_refuses_the_ap_image(self):
        data = need(AP_EXE)
        with self.assertRaises(ValueError):
            gen.decode(data, gen.identity.parse(data))


if __name__ == '__main__':
    unittest.main()
