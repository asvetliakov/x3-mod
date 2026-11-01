"""Host tests of the FL light-candidate constants (src/engine/fl/sun_light_poll_fl_inc.h).

docs/reverse-engineering/fl/sun-light.md, "2026-11-01: poll contract". (a) The fragment is generated from
X3FL.exe on disk and carries the FL layout (colour +0x154, range +0x15c, record +0x170; AP +0x150/+0x158/
+0x16c). (b) A Python mirror of the in-memory anchor check accepts every anchor relocated to the preferred
base, FL run 1's live base 0x4d0000 and a high base, and refuses a flipped byte, a foreign base and an
unreadable span. (c) The generator refuses an image whose instructions carry the AP layout, another
global, another threshold or an extra collector caller. (d) The fragment compiles standalone when the
MinGW cross compiler is present. X3FL.exe is only read.
"""
import importlib.util
from pathlib import Path
import re
import shutil
import struct
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
FL_EXE = Path('/mnt/c/X3FL/X3FL.exe')
FRAGMENT = ROOT / 'src/engine/fl/sun_light_poll_fl_inc.h'
LIVE_BASE = 0x4d0000  # FL run 1 (docs/verification/farnhams-legacy.md)
NO_RELOCATION = 0xff

spec = importlib.util.spec_from_file_location('fl_sun_light_constants', ROOT / 'tools/analysis/fl_sun_light_constants.py')
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)


def need(path):
    if not path.exists():
        raise unittest.SkipTest(f'{path} absent')
    return path.read_bytes()


def constants():
    """Scalars and anchors of the generated C++ fragment."""
    text = FRAGMENT.read_text()
    scalars = {m.group(1): int(m.group(2), 0) for m in re.finditer(r'\b(\w+) = (0x[0-9a-f]+|\d+)\b(?![.])', text)}
    anchors = {}
    for m in re.finditer(r'\{(0x[0-9a-f]+), (\d+), (0x[0-9a-f]+), (0x[0-9a-f]+), \{([^}]*)\}\}, // (\w+)', text):
        length = int(m.group(2))
        raw = bytes(int(b, 16) for b in m.group(5).split(','))
        anchors[m.group(6)] = {'rva': int(m.group(1), 16), 'length': length, 'reloc_offset': int(m.group(3), 16),
                               'target_rva': int(m.group(4), 16), 'bytes': raw[:length], 'padding': raw[length:]}
    return scalars, anchors


def anchor_matches(image_read, base, a):
    """Python mirror of the reader's anchor check: image_read(address, size) -> bytes or None."""
    actual = image_read(base + a['rva'], a['length'])
    if actual is None:
        return False
    masked = bytearray(actual)
    if a['reloc_offset'] != NO_RELOCATION:
        if struct.unpack_from('<I', actual, a['reloc_offset'])[0] != (base + a['target_rva']) & 0xffffffff:
            return False
        masked[a['reloc_offset']:a['reloc_offset'] + 4] = b'\0\0\0\0'
    return bytes(masked) == a['bytes']


def relocated_span(data, fields, a, base):
    """The file's span with its HIGHLOW relocation (if any) applied for `base`, as the loader maps it."""
    off = gen.identity.rva_to_offset(fields, a['rva'])
    span = bytearray(data[off:off + a['length']])
    if a['reloc_offset'] != NO_RELOCATION:
        value = struct.unpack_from('<I', span, a['reloc_offset'])[0] - fields['image_base'] + base
        struct.pack_into('<I', span, a['reloc_offset'], value & 0xffffffff)
    return bytes(span)


class FlSunLightConstants(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = need(FL_EXE)
        cls.fields = gen.identity.parse(cls.data)

    def test_fragment_is_generated_from_x3fl(self):
        self.assertEqual(FRAGMENT.read_text(), gen.fragment(self.data, self.fields))

    def test_fl_layout_and_matched_functions(self):
        s, anchors = constants()
        self.assertEqual(s['context_slot_rva'], 0x1aa100)  # the engine global of object_trace_fl_inc.h
        self.assertEqual((s['array_offset'], s['array_words'], s['array_capacity']), (0x5e8c, 255, 254))
        self.assertEqual((s['slot_count_offset'], s['slot_count_max']), (0x6288, 8))
        self.assertEqual(s['slot_count_offset'], s['array_offset'] + 4 * s['array_words'])
        self.assertEqual((s['node_flags'], s['node_position']), (0x12c, 0xb0))
        self.assertEqual((s['node_colour'], s['node_range'], s['node_record']), (0x154, 0x15c, 0x170))  # AP +4 each
        self.assertEqual((s['light_bit'], s['directional_bit'], s['excluded_bits']), (4, 0x800000, 0x400010))
        self.assertEqual((s['directional_bonus'], s['range_threshold']), (0x300, 0x256250))
        self.assertEqual((s['collector_rva'], s['selector_rva'], s['dir_rule_rva'], s['lazy_flag_rva']),
                         (0x95450, 0x964c0, 0xe6f6d, 0xde1c0))
        self.assertEqual(len(anchors), s['anchor_count'])
        context = sorted(name for name, a in anchors.items() if a['target_rva'] == s['context_slot_rva'])
        self.assertEqual(context, ['collector_context', 'context_publish', 'context_restore', 'dir_rule_context',
                                   'selector_context', 'slot_count_source'])
        for a in anchors.values():
            self.assertFalse(any(a['padding']))
            self.assertEqual(a['reloc_offset'] == NO_RELOCATION, a['target_rva'] == 0)
        text = FRAGMENT.read_text()
        for colour, value in (('green', 0.587), ('red', 0.299), ('blue', 0.114)):
            self.assertIn(f'constexpr double luma_{colour} = {value!r};', text)

    def test_in_memory_mirror_accepts_relocated_spans_and_refuses_mutations(self):
        _, anchors = constants()
        for name, a in anchors.items():
            for base in (self.fields['image_base'], LIVE_BASE, 0x10000000):
                image = {base + a['rva']: relocated_span(self.data, self.fields, a, base)}
                read = lambda address, size, image=image: image.get(address) if len(image.get(address, b'')) == size else None
                self.assertTrue(anchor_matches(read, base, a), (name, hex(base)))
                if a['reloc_offset'] != NO_RELOCATION:
                    shifted = {base + 0x10000 + a['rva']: image[base + a['rva']]}
                    read_shifted = lambda address, size, image=shifted: image.get(address)
                    self.assertFalse(anchor_matches(read_shifted, base + 0x10000, a), (name, 'foreign base'))
                for i in range(a['length']):
                    flipped = bytearray(image[base + a['rva']])
                    flipped[i] ^= 0x01
                    read_flipped = lambda address, size, span=bytes(flipped): span if size == len(span) else None
                    self.assertFalse(anchor_matches(read_flipped, base, a), (name, hex(base), i))
                self.assertFalse(anchor_matches(lambda address, size: None, base, a))

    def mutated(self, rva, old, new):
        off = gen.identity.rva_to_offset(self.fields, rva)
        self.assertEqual(self.data[off:off + len(old)], old)
        image = bytearray(self.data)
        image[off:off + len(new)] = new
        return bytes(image)

    def test_generator_refuses_ap_layout_and_foreign_values(self):
        cases = (
            # selector red word +0x154 -> AP +0x150
            (0x9654b, bytes.fromhex('0fbf8754010000'), bytes.fromhex('0fbf8750010000')),
            # Dir-slot range +0x15c -> AP +0x158
            (0xe6fa5, bytes.fromhex('81b85c010000'), bytes.fromhex('81b858010000')),
            # Dir-slot threshold 0x256250 -> 0x256251
            (0xe6fab, bytes.fromhex('50622500'), bytes.fromhex('51622500')),
            # lazy setter record +0x170 -> AP +0x16c
            (0xde1c0, bytes.fromhex('8b9170010000'), bytes.fromhex('8b916c010000')),
            # directional bonus 0x300 -> 0x200
            (0x96596, bytes.fromhex('81c600030000'), bytes.fromhex('81c600020000')),
            # collector context global names the next dword
            (0x95459, struct.pack('<I', 0x5aa100), struct.pack('<I', 0x5aa104)),
            # selector green weight reads the red constant
            (0x96527, struct.pack('<I', 0x591b38), struct.pack('<I', 0x591b10)),
            # collector array offset 0x5e8c -> 0x5e90
            (0x95474, bytes.fromhex('8c5e0000'), bytes.fromhex('905e0000')),
            # slot clamp 8 -> 9
            (0xda1c8, bytes.fromhex('08000000'), bytes.fromhex('09000000')),
        )
        for rva, old, new in cases:
            with self.subTest(rva=hex(rva)):
                with self.assertRaises(ValueError):
                    gen.decode(self.mutated(rva, old, new), self.fields)

    def test_generator_refuses_a_moved_relocation_and_an_extra_caller(self):
        # A HIGHLOW entry for 0x95472 (lea ebx,[ecx+0x5e8c]) instead of 0x95459: the anchor would be relocated
        # at a field offset. Find the reloc entry naming 0x95459 and point it at 0x95474.
        data = bytearray(self.data)
        pos = gen.identity.rva_to_offset(self.fields, self.fields['reloc_rva'])
        end = pos + self.fields['reloc_size']
        patched = False
        while pos + 8 <= end and not patched:
            page, block = struct.unpack_from('<II', data, pos)
            for k in range((block - 8) // 2):
                entry = struct.unpack_from('<H', data, pos + 8 + 2 * k)[0]
                if entry >> 12 == 3 and page + (entry & 0xfff) == 0x95459:
                    struct.pack_into('<H', data, pos + 8 + 2 * k, 0x3000 | (0x95474 - page))
                    patched = True
                    break
            pos += block
        self.assertTrue(patched)
        with self.assertRaises(ValueError):
            gen.decode(bytes(data), self.fields)
        # A fourth direct caller of the collector (int3 padding after it turned into `call collector`).
        pad = 0x95667
        rel = gen.COLLECTOR_RVA - (pad + 5)
        image = self.mutated(pad, b'\xcc' * 5, b'\xe8' + struct.pack('<i', rel))
        with self.assertRaises(ValueError):
            gen.decode(image, self.fields)

    def test_fragment_compiles_standalone(self):
        compiler = shutil.which('i686-w64-mingw32-g++')
        if not compiler:
            self.skipTest('i686-w64-mingw32-g++ absent')
        subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsyntax-only', '-x', 'c++', str(FRAGMENT)],
                       check=True)


if __name__ == '__main__':
    unittest.main()
