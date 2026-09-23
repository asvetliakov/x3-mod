"""The cull census's body-table read (`body=` field) pinned against the installed EXE.

cull_census_core.h reads the body name of a model id through the engine's body
table (docs/reverse-engineering/body-format-bob1.md 6): manager pointer at the
image global 0x00608518, fixed count +0xb4 (11000), dynamic count +0xb8, slot
array +0xbc with 0x1c-byte slots, name char* at slot +0x0c. The read is data
only (no hook site), so this test pins the engine code that encodes those
fields: the manager init 0x0046d910, the id -> name function 0x0046df60 and the
get-or-register scan 0x0046e400, including the four signed branches that bound
the id in 0x0046df60. An EXE change that moves any of them fails
here; the core constants are parsed from the header and must match the
encodings. Reads the installed EXE only when present. No Wine, no game.
"""
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import verify_chase_aim_sites as common  # noqa: E402
from verification.analysis.test_chase_aim_sites import synthetic_image  # noqa: E402

CORE = ROOT / 'src/proxy/cull_census_core.h'

# (va, bytes, instruction) in the installed EXE (exe_identity.identity_ok).
PATTERNS = (
    # 0x0046d910: body manager init
    (0x46d912, '8b3d18856000', 'mov edi,[0x00608518]'),
    (0x46d91d, 'c787b4000000f82a0000', 'mov dword [edi+0xb4],11000'),
    # 0x0046df60: id -> slot -> name
    (0x46df63, '81fae8030000', 'cmp edx,1000'),
    (0x46df69, '8b0d18856000', 'mov ecx,[0x00608518]'),
    (0x46df76, '7d04', 'jge 0x46df7c (id >= 1000, signed)'),
    (0x46df7c, '81fa204e0000', 'cmp edx,20000'),
    (0x46df82, '7d08', 'jge 0x46df8c (id >= 20000, signed)'),
    (0x46df84, '8d82d8dcffff', 'lea eax,[edx-9000]'),
    (0x46df8c, '8b81b4000000', 'mov eax,[ecx+0xb4]'),
    (0x46df92, '8d8410e0b1ffff', 'lea eax,[eax+edx-20000]'),
    (0x46df9c, '7c6d', 'jl 0x46e00b (slot < 0 refused, signed)'),
    (0x46df9e, '8bb9b8000000', 'mov edi,[ecx+0xb8]'),
    (0x46dfa4, '03b9b4000000', 'add edi,[ecx+0xb4]'),
    (0x46dfac, '7d5d', 'jge 0x46e00b (slot >= fixed + dynamic refused, signed)'),
    (0x46dfae, '8b89bc000000', 'mov ecx,[ecx+0xbc]'),
    (0x46dfb4, '8d3cc500000000', 'lea edi,[eax*8]'),
    (0x46dfbb, '2bf8', 'sub edi,eax (7*slot)'),
    (0x46dfbd, '8b44b90c', 'mov eax,[ecx+edi*4+0xc] (slot*0x1c+0x0c)'),
    # 0x0046e400: the scan over every slot's name (manager in esi)
    (0x46e4b3, '8b96b8000000', 'mov edx,[esi+0xb8]'),
    (0x46e4b9, '0396b4000000', 'add edx,[esi+0xb4]'),
    (0x46e4d0, '8b96bc000000', 'mov edx,[esi+0xbc]'),
    (0x46e4d6, '8d0cc500000000', 'lea ecx,[eax*8]'),
    (0x46e4dd, '2bc8', 'sub ecx,eax (7*slot)'),
    (0x46e4df, '8b4c8a0c', 'mov ecx,[edx+ecx*4+0xc] (slot*0x1c+0x0c)'),
)


def core_constants(text):
    def value(name):
        match = re.search(rf'\b{name}\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*[;,]', text)
        return int(match.group(1), 0) if match else None
    return {n: value(n) for n in ('body_global_va', 'body_fixed_count_offset', 'body_dynamic_count_offset', 'body_slots_offset',
                                  'body_slot_stride', 'body_slot_name_offset', 'body_fixed_count')}


def inspect(data):
    """Per-pattern match on an image -> {va: bool}."""
    image = common.Image(data)
    return {va: image.read(va, len(bytes.fromhex(h))) == bytes.fromhex(h) for va, h, _ in PATTERNS}


def le32(value):
    return value.to_bytes(4, 'little').hex()


class BodyTableExe(unittest.TestCase):
    def test_patterns_encode_the_core_constants(self):
        c = core_constants(CORE.read_text())
        self.assertEqual(c, {'body_global_va': 0x00608518, 'body_fixed_count_offset': 0xb4, 'body_dynamic_count_offset': 0xb8,
                             'body_slots_offset': 0xbc, 'body_slot_stride': 0x1c, 'body_slot_name_offset': 0x0c, 'body_fixed_count': 11000})
        encoded = {h for _, h, _ in PATTERNS}
        self.assertTrue({'8b3d' + le32(c['body_global_va']), '8b0d' + le32(c['body_global_va'])} <= encoded, 'the global')
        self.assertIn('c787' + le32(c['body_fixed_count_offset']) + le32(c['body_fixed_count']), encoded)
        for field in ('body_fixed_count_offset', 'body_dynamic_count_offset', 'body_slots_offset'):
            self.assertGreaterEqual(sum(1 for h in encoded if len(h) == 12 and h[4:] == le32(c[field])), 2, field)   # both functions read it
        # lea r,[slot*8]; sub r,slot; mov r,[base+r*4+disp8]: stride 7*4, displacement the name offset.
        self.assertEqual(c['body_slot_stride'], 7 * 4)
        self.assertTrue({'8b44b9' + f'{c["body_slot_name_offset"]:02x}', '8b4c8a' + f'{c["body_slot_name_offset"]:02x}'} <= encoded)
        # The id bounds are signed and point the way body_slot() maps them: jge/jge after the 1000/20000
        # compares, jl on a negative slot and jge on slot >= fixed + dynamic (both to the refusal at 0x46e00b).
        branches = {va: h for va, h, _ in PATTERNS if va in (0x46df76, 0x46df82, 0x46df9c, 0x46dfac)}
        self.assertEqual({va: h[:2] for va, h in branches.items()}, {0x46df76: '7d', 0x46df82: '7d', 0x46df9c: '7c', 0x46dfac: '7d'})
        self.assertEqual({va: va + 2 + int(h[2:], 16) for va, h in branches.items()},
                         {0x46df76: 0x46df7c, 0x46df82: 0x46df8c, 0x46df9c: 0x46e00b, 0x46dfac: 0x46e00b})

    def test_changed_byte_is_caught(self):
        extra = [(va, bytes.fromhex(h)) for va, h, _ in PATTERNS]
        self.assertTrue(all(inspect(synthetic_image(extra=extra, text_size=0x100000)).values()))
        for va, h, _ in PATTERNS:
            raw = bytearray(bytes.fromhex(h))
            raw[-1] ^= 0x01
            changed = [(v, bytes(raw) if v == va else bytes.fromhex(x)) for v, x, _ in PATTERNS]
            result = inspect(synthetic_image(extra=changed, text_size=0x100000))
            self.assertFalse(result[va], hex(va))
            self.assertEqual(sum(result.values()), len(PATTERNS) - 1, hex(va))

    @unittest.skipUnless(common.DEFAULT_EXE.is_file(), 'installed executable not present')
    def test_installed_executable(self):
        import exe_identity
        data = common.image_bytes(common.DEFAULT_EXE)
        self.assertTrue(exe_identity.identity_ok(data))  # structure + anchors; the raw hash is INFO only
        self.assertEqual([hex(va) for va, ok in inspect(data).items() if not ok], [])


if __name__ == '__main__':
    unittest.main()
