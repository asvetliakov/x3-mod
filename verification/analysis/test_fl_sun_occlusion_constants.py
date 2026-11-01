"""Host tests of the FL sun-occlusion constants (src/engine/fl/sun_occlusion_fl_inc.h).

docs/reverse-engineering/fl/sun-occlusion.md, "Verified hook sites and ABI (2026-11-01)". The fragment is generated
from X3FL.exe on disk by tools/analysis/fl_sun_occlusion_constants.py: the probe CALL 0x8af0a (ECX = record, EDX =
view, no stack argument, bare RETs, the caller's record+0x30 = 0 store) and the lens CALL 0x8bcb7 (EDX = view, no
stack argument, one bare RET, no EBP) are whole five-byte instructions no branch or relocation reaches; the 147-byte
gate interval matches the fl-codex FNV-1a; every anchor's relocations name documented slots; the record and view
offsets equal the AP core's except the config flags word (+0xec, AP +0xfc). A Python mirror of the runtime anchor
check accepts each span relocated to the preferred base, FL run 1's live base and 0x10000000 and refuses a flipped
byte, a foreign base and an unreadable span. The generator refuses swapped probe registers, AP's config offset, a
caller stack read, a popping RET, a +0x30 store in the probe, a stack argument or EBP use at the lens site, a
padding word read, an added caller, a branch into a CALL, a moved loop edge and the AP image. X3FL.exe is only read.
"""
import importlib.util
from pathlib import Path
import re
import struct
import unittest

ROOT = Path(__file__).resolve().parents[2]
FL_EXE = Path('/mnt/c/X3FL/X3FL.exe')
AP_EXE = Path('/mnt/c/X3AP-orig/X3AP.exe')
FRAGMENT = ROOT / 'src/engine/fl/sun_occlusion_fl_inc.h'
AP_CORE = ROOT / 'src/proxy/sun_occlusion_core.h'
LIVE_BASE = 0x4d0000  # FL run 1 (docs/verification/farnhams-legacy.md)

spec = importlib.util.spec_from_file_location('fl_sun_occlusion_constants',
                                              ROOT / 'tools/analysis/fl_sun_occlusion_constants.py')
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
    v = re.search(rf'\b{name} = (0x[0-9a-f]+|\d+|true|false)(?:ull)?[;,]', FRAGMENT.read_text()).group(1)
    return {'true': True, 'false': False}[v] if v in ('true', 'false') else int(v, 0)


def ap_core_value(name):
    return int(re.search(rf'\b{name} = (0x[0-9a-f]+|\d+)', AP_CORE.read_text()).group(1), 0)


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


class FlSunOcclusionConstants(unittest.TestCase):
    def test_fragment_is_generated_from_x3fl(self):
        data = need(FL_EXE)
        self.assertEqual(FRAGMENT.read_text(), gen.fragment(data, gen.identity.parse(data)))

    def test_documented_sites_abi_and_layout(self):
        data = need(FL_EXE)
        d = gen.decode(data, gen.identity.parse(data))
        expected = {'probe_site_rva': 0x8af0a, 'probe_target_rva': 0xa3fd0, 'lens_site_rva': 0x8bcb7,
                    'lens_target_rva': 0x97680, 'probe_walk_rva': 0x8aeb0, 'root_slot_rva': 0x1aa100,
                    'config_slot_rva': 0x1a83b0, 'mode_slot_rva': 0x1a82f8, 'config_video_flags': 0xec,
                    'video_flag_flares': 0x8000, 'record_bytes': 0x70, 'probe_gates_length': 147,
                    'root_lens_holder': 0x64, 'root_draw_list': 0x40, 'compositor_rva': 0xe6770}
        for name, value in expected.items():
            self.assertEqual(fragment_value(name), value, name)
        # The AP core's record and view layout holds on FL unchanged; only the config flags word moved (0xfc -> 0xec).
        for name in ('record_view', 'record_accumulator', 'record_x', 'record_y', 'record_visible', 'record_size',
                     'record_group', 'view_flags', 'view_rect_top', 'view_rect_bottom', 'view_rect_left',
                     'view_rect_right', 'view_fov', 'view_scale_x', 'view_scale_y', 'view_flag_probe_hidden',
                     'video_flag_flares'):
            fl_name = 'record_owner' if name == 'record_view' else name
            self.assertEqual(fragment_value(fl_name), ap_core_value(name), name)
        self.assertEqual(ap_core_value('config_video_flags'), 0xfc)
        self.assertEqual(ap_core_value('probe_gates_length'), 148)
        self.assertEqual(d['gates_fnv1a_file'], 0x1abcc319ce5e4445)  # the fl-codex figure
        self.assertEqual(fragment_value('probe_gates_fnv1a_masked'), d['gates_fnv1a_masked'])
        self.assertEqual(d['probe_target_callers'], [0x8af0a])
        self.assertEqual(d['probe_walk_callers'], [0x8bb96])
        self.assertEqual(d['lens_target_callers'], [0x8ba8d, 0x8bcb7, 0x978ab])
        self.assertEqual(d['pass_callers'], [0x782cf])
        self.assertEqual(d['probe_rets'], [0xa4011, 0xa42db])
        self.assertEqual(d['lens_rets'], [0x97714])
        self.assertEqual((d['probe_target_instructions'], d['lens_target_instructions']), (220, 47))
        # Probe CALL inside one qword (one lock cmpxchg8b); the lens CALL straddles one (claim_call's plain copy).
        self.assertEqual((d['probe_site_atomic'], d['lens_site_atomic']), (True, False))
        self.assertIs(fragment_value('probe_site_atomic'), True)
        self.assertIs(fragment_value('lens_site_atomic'), False)
        self.assertEqual(d['pass_order'], sorted(d['pass_order']))

    def test_every_relocation_is_a_named_slot(self):
        data = need(FL_EXE)
        fields = gen.identity.parse(data)
        relocated = gen.identity.relocated_rvas(data, fields)
        anchors = fragment_anchors()
        self.assertEqual(len(anchors), len(gen.ANCHORS))
        self.assertTrue(all(a['length'] <= 160 and len(a['offsets']) <= 6 for a in anchors.values()))
        slots = {fragment_value(n) for n in gen.SLOTS}
        covered = set()
        for name, a in anchors.items():
            inside = sorted(r - a['rva'] for r in relocated if a['rva'] - 3 <= r < a['rva'] + a['length'])
            self.assertEqual(inside, a['offsets'], name)
            covered |= set(a['slots'])
        self.assertEqual(slots, covered)
        # Both CALLs are inside an anchor (checked before the claim) and carry their file rel32.
        for site, name in ((fragment_value('probe_site_rva'), 'probe_walk'), (fragment_value('lens_site_rva'), 'lens_block')):
            a = anchors[name]
            self.assertTrue(a['rva'] <= site and site + 5 <= a['rva'] + a['length'], name)
            self.assertEqual(a['bytes'][site - a['rva']], 0xe8, name)

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

    def test_generator_refuses_abi_and_layout_changes(self):
        data = need(FL_EXE)
        fields = gen.identity.parse(data)
        cases = (
            (0x08af06, bytes([0x8b, 0xd6])),                         # probe: mov edx,esi (record as the view)
            (0x0a3ffc, bytes([0xfc])),                               # AP's config flags word +0xfc
            (0x0a405d, bytes([0x04])),                               # hidden flag 0x4000000
            (0x0a4081, bytes([0x8b, 0x8d, 0x08, 0, 0, 0])),         # probe body: mov ecx,[ebp+8], a stack argument
            (0x0a42db, bytes([0xc2])),                               # probe: ret imm16
            (0x0a41d0, bytes([0xc7, 0x46, 0x30])),                   # probe body stores at +0x30
            (0x08bcb5, bytes([0x56, 0x90])),                         # lens: push esi; nop (a stack argument)
            (0x097714, bytes([0xc2])),                               # lens: ret imm16
            (0x097688, bytes([0x8b, 0xdd])),                         # lens: mov ebx,ebp
            (0x08bcc1, bytes([0x8b, 0xf1])),                         # lens continuation reads ECX
            (0x0de57e, bytes([0x0c])),                               # instance helper reads the padding word
            (0x08af3b, bytes([0xe8]) + struct.pack('<i', 0xa3fd0 - 0x8af40)),  # a second probe caller
            (0x097715, bytes([0xe9]) + struct.pack('<i', 0x8af0b - 0x9771a)),  # a branch into the probe CALL
            (0x08bba9, struct.pack('<i', 0x8b918 - 0x8bbad)),        # sorted-loop back edge moved
            (0x08bcb8, struct.pack('<i', 0x97681 - 0x8bcbc)),        # lens CALL retargeted
            (0x0972dc, bytes([0x90])),                               # split instruction in the record link
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
