"""Tests of the read-only hook-site probe verification/probe/verify_chase_camera_site.py.

A synthetic PE image carries the documented bytes at the documented VAs; the
probe must pass on it and fail closed on each corruption (changed site bytes,
a short branch inside the span, a jump into the interior, the wrong main-loop
order). The installed X3AP.exe, when present, is checked as well (read-only).
The FL verifier (--edition fl) runs on the installed X3FL.exe, when present, and on in-memory mutations of its bytes
(site byte, an injected interior edge, a relocation inside the span, the EBX reload, a field offset, the plane slot).
"""
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import verify_chase_camera_site as probe  # noqa: E402


def synthetic_image(site=probe.SITE_BYTES, extra=()):
    """One .text section at 0x00401000 covering the function and the main loop."""
    text_va, text_size = 0x00401000, 0x80000
    text = bytearray(b'\x90' * text_size)

    def put(va, data):
        text[va - text_va:va - text_va + len(data)] = data

    put(probe.SITE_VA, site)
    put(probe.FUNCTION_START, probe.FUNCTION_PROLOGUE)
    for va, target in probe.MAIN_LOOP_CALLS:
        put(va, b'\xe8' + struct.pack('<i', target - (va + 5)))
    for va, data in extra:
        put(va, data)
    pe_offset = 0x80
    header = bytearray(b'\0' * 0x400)
    header[0:2] = b'MZ'
    struct.pack_into('<I', header, 0x3c, pe_offset)
    header[pe_offset:pe_offset + 4] = b'PE\0\0'
    struct.pack_into('<H', header, pe_offset + 6, 1)          # sections
    optional = 0xe0
    struct.pack_into('<H', header, pe_offset + 20, optional)
    struct.pack_into('<I', header, pe_offset + 24 + 28, probe.IMAGE_BASE)
    s = pe_offset + 24 + optional
    header[s:s + 8] = b'.text\0\0\0'
    struct.pack_into('<IIII', header, s + 8, text_size, text_va - probe.IMAGE_BASE, text_size, 0x400)
    return bytes(header) + bytes(text)


class SyntheticImage(unittest.TestCase):
    def test_documented_bytes_pass_except_identity(self):
        report = probe.verify(synthetic_image())
        checks = report['checks']
        self.assertTrue(checks['site_bytes']['ok'])
        self.assertTrue(checks['relocation']['ok'], checks['relocation'])
        self.assertEqual([i['length'] for i in checks['relocation']['instructions']], [4, 6])
        self.assertEqual(checks['relocation']['instructions'][1]['target'], f'{probe.SITE_BRANCH_TARGET:#010x}')
        self.assertTrue(checks['interior_branches']['ok'])
        self.assertTrue(checks['function_prologue']['ok'])
        self.assertTrue(checks['main_loop_order']['ok'])
        self.assertFalse(checks['size']['ok'])  # the synthetic image is not the game
        self.assertEqual(report['result'], 'FAIL')

    def test_site_byte_change_fails(self):
        bad = bytearray(probe.SITE_BYTES); bad[2] = 0x58
        report = probe.verify(synthetic_image(site=bytes(bad)))
        self.assertFalse(report['checks']['site_bytes']['ok'])
        self.assertEqual(report['result'], 'FAIL')

    def test_short_branch_in_the_span_is_refused(self):
        bad = probe.SITE_BYTES[:4] + b'\x74\x10' + probe.SITE_BYTES[6:]
        report = probe.verify(synthetic_image(site=bad))
        self.assertFalse(report['checks']['relocation']['ok'])
        self.assertIn('short branch', report['checks']['relocation']['error'])

    def test_undecoded_opcode_is_refused(self):
        bad = b'\xc7' + probe.SITE_BYTES[1:]
        report = probe.verify(synthetic_image(site=bad))
        self.assertFalse(report['checks']['relocation']['ok'])
        self.assertIn('undecoded', report['checks']['relocation']['error'])

    def test_jump_into_the_interior_is_reported(self):
        interior = probe.SITE_VA + 4  # the jz itself
        at = probe.FUNCTION_START + 0x100
        jump = b'\xe9' + struct.pack('<i', interior - (at + 5))
        report = probe.verify(synthetic_image(extra=[(at, jump)]))
        self.assertFalse(report['checks']['interior_branches']['ok'])
        self.assertEqual(report['checks']['interior_branches']['hits'][0]['target'], f'{interior:#010x}')

    def test_main_loop_order_is_checked(self):
        va, target = probe.MAIN_LOOP_CALLS[0]
        wrong = b'\xe8' + struct.pack('<i', target + 0x10 - (va + 5))
        report = probe.verify(synthetic_image(extra=[(va, wrong)]))
        self.assertFalse(report['checks']['main_loop_order']['ok'])

    def test_rebased_rel32_keeps_the_target(self):
        # What engine_patch::claim does with rel32_offset: the same absolute target from the tail.
        tail = 0x10000000
        original = struct.unpack_from('<i', probe.SITE_BYTES, probe.REL32_OFFSET)[0]
        target = probe.SITE_VA + probe.REL32_OFFSET + 4 + original
        rebased = target - (tail + probe.REL32_OFFSET + 4)
        self.assertEqual(target, probe.SITE_BRANCH_TARGET)
        self.assertEqual(tail + probe.REL32_OFFSET + 4 + rebased, target)


class InstalledExecutable(unittest.TestCase):
    def test_installed_game_matches_the_study(self):
        if not probe.DEFAULT_EXE.is_file():
            self.skipTest('installed X3AP.exe not found')
        report = probe.verify(probe.DEFAULT_EXE.read_bytes())
        self.assertEqual(report['result'], 'PASS', report)
        self.assertFalse(report['checks']['atomic_write']['qword_aligned_span'])  # documented: plain copy in the install window


class InstalledFlExecutable(unittest.TestCase):
    """The --edition fl verifier on the installed X3FL.exe (read-only) and on in-memory mutations of its bytes."""

    @classmethod
    def setUpClass(cls):
        if not probe.FL_DEFAULT_EXE.is_file():
            raise unittest.SkipTest('installed X3FL.exe not found')
        cls.data = probe.FL_DEFAULT_EXE.read_bytes()
        cls.image = probe.FlImage(cls.data)

    def mutate(self, rva, new):
        data = bytearray(self.data)
        o = self.image.file_offset(probe.IMAGE_BASE + rva)
        data[o:o + len(new)] = new
        return probe.verify_fl(bytes(data))

    def test_installed_fl_game_matches_the_study(self):
        report = probe.verify_fl(self.data)
        self.assertEqual(report['result'], 'PASS', {k: v for k, v in report['checks'].items() if not v.get('ok')})
        checks = report['checks']
        self.assertEqual(checks['inbound_edges']['to_start'], [f'{a:#08x}' for a in probe.FL_INBOUND_EDGES])
        self.assertEqual(checks['inbound_edges']['updater_callers'], ['0x01e5b0'])
        self.assertEqual(checks['relocatable']['highlow'], probe.FL_HIGHLOW_COUNT)
        self.assertEqual(checks['span_decode']['span_lengths'], [4, 6])
        self.assertFalse(checks['atomic_write']['qword_aligned_span'])
        self.assertEqual(checks['field_anchors']['count'], len(probe.FL_FIELDS))

    def test_changed_site_byte_fails(self):
        checks = self.mutate(probe.FL_SITE_RVA + 2, b'\x58')['checks']
        self.assertFalse(checks['site_bytes']['ok'])
        self.assertFalse(checks['windows']['ok'])

    def test_injected_interior_edge_fails(self):
        # The first inbound je (0f 84 rel32 at 0x022141) re-aimed at the span's je (site+4).
        at = probe.FL_INBOUND_EDGES[0]
        rel = probe.FL_SITE_RVA + 4 - (at + 6)
        checks = self.mutate(at + 2, struct.pack('<i', rel))['checks']
        self.assertFalse(checks['inbound_edges']['ok'])
        self.assertEqual(checks['inbound_edges']['interior'], [f'{at:#08x}->{probe.FL_SITE_RVA + 4:#08x}'])

    def test_relocation_inside_the_span_fails(self):
        # The first HIGHLOW entry of the span's page re-pointed into the span (site+2).
        pe = struct.unpack_from('<I', self.data, 0x3c)[0]
        reloc_rva, size = struct.unpack_from('<II', self.data, pe + 24 + 96 + 5 * 8)
        o = self.image.file_offset(probe.IMAGE_BASE + reloc_rva)
        end, page = o + size, probe.FL_SITE_RVA & ~0xfff
        while struct.unpack_from('<I', self.data, o)[0] != page:
            o += struct.unpack_from('<I', self.data, o + 4)[0]
            self.assertLess(o, end)
        data = bytearray(self.data)
        struct.pack_into('<H', data, o + 8, 0x3000 | ((probe.FL_SITE_RVA + 2) & 0xfff))
        checks = probe.verify_fl(bytes(data))['checks']
        self.assertFalse(checks['reloc_span']['ok'])
        self.assertEqual(checks['reloc_span']['fixups_touching_span'], [f'{probe.FL_SITE_RVA + 2:#08x}'])

    def test_ebx_reload_from_another_slot_fails(self):
        checks = self.mutate(0x0226d8 + 3, b'\x18')['checks']  # mov ebx,[esp+0x18]
        self.assertFalse(checks['ebx_cockpit']['ok'])

    def test_changed_field_offset_fails(self):
        checks = self.mutate(0x0224f9 + 2, b'\x5c')['checks']  # mov eax,[ebx+0x5c]
        self.assertFalse(checks['field_anchors']['ok'])
        self.assertEqual(checks['field_anchors']['failed'][0]['rva'], '0x0224f9')

    def test_moved_plane_slot_fails(self):
        checks = self.mutate(0x0dec8d, struct.pack('<I', probe.IMAGE_BASE + probe.FL_PLANE_SLOT_RVA + 4))['checks']
        self.assertFalse(checks['reloc_plane_slot']['ok'])


if __name__ == '__main__':
    unittest.main()
