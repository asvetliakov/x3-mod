"""Tests of the read-only hook-site probe verification/probe/verify_chase_camera_site.py.

A synthetic PE image carries the documented bytes at the documented VAs; the
probe must pass on it and fail closed on each corruption (changed site bytes,
a short branch inside the span, a jump into the interior, the wrong main-loop
order). The installed X3AP.exe, when present, is checked as well (read-only).
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
        report = probe.verify(synthetic_image(), sha256=probe.EXPECTED_SHA256)
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


if __name__ == '__main__':
    unittest.main()
