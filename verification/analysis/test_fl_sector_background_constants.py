"""Host tests of the FL sector-background constants (src/engine/fl/sector_background_fl_inc.h).

docs/reverse-engineering/fl/sector-fog.md section 6. The fragment is generated from X3FL.exe on disk by
tools/analysis/fl_sector_background_constants.py: every anchor is a run of whole instructions, every base
relocation in it names a documented .data slot, and every field offset is read from a named native operand.
A Python mirror of the runtime anchor check accepts each span relocated to the preferred base, FL run 1's
live base 0x4d0000 and 0x10000000, and refuses a flipped byte, a foreign base and an unreadable span. The
generator refuses a moved slot, a changed field operand, a changed stride and the AP image. X3FL.exe is
only read.
"""
import importlib.util
from pathlib import Path
import re
import struct
import unittest

ROOT = Path(__file__).resolve().parents[2]
FL_EXE = Path('/mnt/c/X3FL/X3FL.exe')
AP_EXE = Path('/mnt/c/X3AP-orig/X3AP.exe')
FRAGMENT = ROOT / 'src/engine/fl/sector_background_fl_inc.h'
LIVE_BASE = 0x4d0000  # FL run 1 (docs/verification/farnhams-legacy.md)

spec = importlib.util.spec_from_file_location('fl_sector_background_constants',
                                              ROOT / 'tools/analysis/fl_sector_background_constants.py')
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


class FlSectorBackgroundConstants(unittest.TestCase):
    def test_fragment_is_generated_from_x3fl(self):
        data = need(FL_EXE)
        self.assertEqual(FRAGMENT.read_text(), gen.fragment(data, gen.identity.parse(data)))

    def test_documented_slots_and_offsets(self):
        need(FL_EXE)
        expected = {'cockpit_registry_slot_rva': 0x1a82e4, 'background_table_slot_rva': 0x1a83d0,
                    'background_count_slot_rva': 0x1a8338, 'config_slot_rva': 0x1a83b0,
                    'registry_active_handle': 0x10, 'cockpit_ref_object': 0xc, 'cockpit_sector': 0x54,
                    'cockpit_camera': 0x58, 'object_id': 8, 'object_class': 0x48, 'object_parent': 0x54,
                    'sector_class': 1, 'sector_background_index': 0x13c, 'sector_stars': 0x140, 'sector_nebula': 0x144,
                    'record_stride': 0xdb8, 'record_rebase': 0x44, 'record_name': 0, 'record_rates': 0xd0,
                    'record_dust': 0xf0, 'record_fog_near': 0x104, 'record_fog_far': 0x108, 'record_stardust': 0x10c,
                    'camera_flags': 0x270, 'camera_fog_flag': 0x10000, 'camera_fog_near': 0x36c,
                    'camera_fog_far': 0x370, 'config_view_distance': 0x758, 'far_floor_high': 500000000,
                    'far_floor_mid': 100000000, 'record_span': 0x120, 'background_type': 2}
        for name, value in expected.items():
            self.assertEqual(fragment_value(name), value, name)
        # The reader's one row copy stays inside the 0xdb8 row and covers every field it reads.
        self.assertLessEqual(fragment_value('record_rebase') + fragment_value('record_span'), fragment_value('record_stride'))
        self.assertLess(fragment_value('record_stardust') + 4, fragment_value('record_span'))

    def test_every_relocation_is_a_named_slot(self):
        data = need(FL_EXE)
        fields = gen.identity.parse(data)
        relocated = gen.identity.relocated_rvas(data, fields)
        anchors = fragment_anchors()
        self.assertEqual(len(anchors), len(gen.ANCHORS))
        runtime = {fragment_value(n) for n in gen.RUNTIME_SLOTS}
        covered = set()
        for name, a in anchors.items():
            inside = sorted(r - a['rva'] for r in relocated if a['rva'] - 3 <= r < a['rva'] + a['length'])
            self.assertEqual(inside, a['offsets'], name)
            covered |= set(a['slots'])
        self.assertLessEqual(runtime, covered)  # every slot the reader dereferences is anchored

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
            # the inline lookup reading the object manager instead of the cockpit registry
            (0x0cf46f + 1, struct.pack('<I', image_base + 0x1a82ec)),
            # the fog copy reading the count slot as the table
            (0x022e3f + 17, struct.pack('<I', image_base + 0x1a8338)),
            # camera near store at +0x368 instead of +0x36c
            (0x022e66 + 2, struct.pack('<I', 0x368)),
            # stride 0xdb4 in the fog copy's imul
            (0x022e45 + 6, struct.pack('<I', 0xdb4)),
            # the setter's rebase +0x40
            (0x07270f + 2, bytes([0x40])),
            # dust getter reads row +0x138
            (0x072464 + 3, struct.pack('<I', 0x138)),
            # FL's view-distance offset replaced by AP's +0x768
            (0x0e4bcc + 2, struct.pack('<I', 0x768)),
            # class word compared with 2
            (0x043de9 + 4, bytes([2])),
            # a split instruction: the first byte of the fog copy becomes a one-byte opcode
            (0x022e3f, bytes([0x90])),
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
