"""Host tests for the instruction-aware chase aim hook-site verifier."""
import dataclasses
import struct
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import verify_chase_aim_sites as probe  # noqa: E402


def synthetic_image(site_overrides=(), extra=(), text_size=0x80000):
    """Create an objdump-readable one-section PE containing the two functions."""
    text_va = 0x00401000
    text = bytearray(b'\x90' * text_size)

    def put(va, data):
        offset = va - text_va
        text[offset:offset + len(data)] = data

    for site in probe.SITES:
        put(site.va, site.expected)
    for va, data in site_overrides:
        put(va, data)
    for va, data in extra:
        put(va, data)

    pe_offset, header_size = 0x80, 0x400
    header = bytearray(header_size)
    header[0:2] = b'MZ'
    struct.pack_into('<I', header, 0x3c, pe_offset)
    header[pe_offset:pe_offset + 4] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', header, pe_offset + 4,
                     0x14c, 1, 0, 0, 0, 0xe0, 0x0102)
    optional = pe_offset + 24
    struct.pack_into('<H', header, optional, 0x10b)
    struct.pack_into('<I', header, optional + 16, 0x1000)
    struct.pack_into('<I', header, optional + 20, 0x1000)
    struct.pack_into('<I', header, optional + 28, probe.IMAGE_BASE)
    struct.pack_into('<II', header, optional + 32, 0x1000, 0x200)
    struct.pack_into('<I', header, optional + 56, text_size + 0x1000)
    struct.pack_into('<I', header, optional + 60, header_size)
    struct.pack_into('<H', header, optional + 68, 3)
    struct.pack_into('<I', header, optional + 92, 16)
    section = optional + 0xe0
    header[section:section + 8] = b'.text\0\0\0'
    struct.pack_into('<IIIIIIHHI', header, section + 8,
                     text_size, text_va - probe.IMAGE_BASE, text_size,
                     header_size, 0, 0, 0, 0, 0x60000020)
    return bytes(header) + bytes(text)


def verify_synthetic(data):
    with tempfile.NamedTemporaryFile(suffix='.exe') as exe:
        exe.write(data)
        exe.flush()
        return probe.verify_path(exe.name, sha256=probe.EXPECTED_SHA256)


class SourceParity(unittest.TestCase):
    def test_production_specs_match_derived_specs(self):
        text = probe.DEFAULT_SOURCE.read_text(encoding='utf-8')
        self.assertTrue(probe.check_source_specs(text)['ok'])

    def test_changed_source_length_is_rejected(self):
        text = probe.DEFAULT_SOURCE.read_text(encoding='utf-8')
        changed = text.replace(
            '{"chase_fire_ray", 0x00445b70, {0x89,0x45,0xe4,0x8b,0x45,0xe4}, 6,0,0}',
            '{"chase_fire_ray", 0x00445b70, {0x89,0x45,0xe4,0x8b,0x45,0xe4}, 5,0,0}')
        self.assertNotEqual(text, changed)
        self.assertFalse(probe.check_source_specs(changed)['ok'])

    def test_nonzero_relocation_field_is_rejected(self):
        text = probe.DEFAULT_SOURCE.read_text(encoding='utf-8')
        changed = text.replace(
            '{"chase_fire_gate", 0x00445a15, {0x8b,0x43,0x14,0x8b,0xf8}, 5,0,0}',
            '{"chase_fire_gate", 0x00445a15, {0x8b,0x43,0x14,0x8b,0xf8}, 5,1,0}')
        self.assertNotEqual(text, changed)
        self.assertFalse(probe.check_source_specs(changed)['ok'])


class StructuralVerification(unittest.TestCase):
    def test_all_documented_spans_are_whole_plain_instructions(self):
        report = verify_synthetic(synthetic_image())
        sites = report['checks']['sites']['items']
        self.assertTrue(report['checks']['source_specs']['ok'])
        self.assertTrue(report['checks']['function_disassembly']['ok'])
        self.assertTrue(report['checks']['sites']['ok'], sites)
        self.assertEqual([[row['length'] for row in site['instructions']] for site in sites],
                         [[3, 2], [3, 3], [3, 3], [6]])
        self.assertTrue(all(site['plain_no_relative_control'] for site in sites))
        self.assertTrue(all(site['no_interior_branch'] for site in sites))
        self.assertFalse(report['checks']['size']['ok'])
        self.assertEqual(report['result'], 'FAIL')

    def test_each_site_byte_corruption_is_rejected(self):
        for site in probe.SITES:
            with self.subTest(site=site.name):
                changed = bytearray(site.expected)
                changed[-1] ^= 1
                report = verify_synthetic(
                    synthetic_image(site_overrides=[(site.va, bytes(changed))]))
                row = next(item for item in report['checks']['sites']['items']
                           if item['name'] == site.name)
                self.assertFalse(row['bytes_ok'])
                self.assertFalse(row['ok'])

    def test_adjacent_opcode_that_consumes_site_start_is_rejected(self):
        site = probe.SITES[0]
        # The expected five bytes remain present, but EB immediately before them
        # makes objdump decode the first expected byte as the jump displacement.
        report = verify_synthetic(synthetic_image(extra=[(site.va - 1, b'\xeb')]))
        row = report['checks']['sites']['items'][0]
        self.assertTrue(row['bytes_ok'])
        self.assertFalse(row['whole_instructions'])
        self.assertFalse(row['ok'])

    def test_truncated_declared_span_is_not_a_whole_instruction(self):
        data = synthetic_image()
        with tempfile.NamedTemporaryFile(suffix='.exe') as exe:
            exe.write(data)
            exe.flush()
            decoded = probe.disassemble_functions(exe.name)
        image = probe.Image(data)
        original = probe.SITES[0]
        truncated = dataclasses.replace(original, expected=original.expected[:2])
        row = probe.inspect_site(
            image, truncated, decoded[(original.function_start, original.function_end)])
        self.assertTrue(row['bytes_ok'])
        self.assertFalse(row['whole_instructions'])
        self.assertEqual(row['covered_end'], f'{original.va + 3:#010x}')

    def test_real_branch_into_span_interior_is_rejected(self):
        site = probe.SITES[0]
        at = site.function_start + 0x100
        target = site.va + 1
        jump = b'\xe9' + struct.pack('<i', target - (at + 5))
        report = verify_synthetic(synthetic_image(extra=[(at, jump)]))
        row = report['checks']['sites']['items'][0]
        self.assertFalse(row['no_interior_branch'])
        self.assertEqual(row['interior_branches'], [{
            'at': f'{at:#010x}', 'target': f'{target:#010x}', 'mnemonic': 'jmp'}])

    def test_opcode_alias_inside_immediate_is_not_a_branch(self):
        site = probe.SITES[0]
        # A raw scanner sees 74 10 at at+1 and invents a short branch into the
        # patch span.  Objdump correctly treats those bytes as push's immediate.
        at = site.va - 0x11
        alias = b'\x68\x74\x10\x00\x00'
        report = verify_synthetic(synthetic_image(extra=[(at, alias)]))
        row = report['checks']['sites']['items'][0]
        self.assertTrue(row['no_interior_branch'], row['interior_branches'])
        self.assertTrue(row['ok'], row)

    def test_parser_rejects_non_contiguous_function_decode(self):
        text = '  1000:\t90\tnop\n  1002:\t90\tnop\n'
        with self.assertRaisesRegex(ValueError, 'coverage gap'):
            probe.parse_objdump(text, 0x1000, 0x1003)


class InstalledExecutable(unittest.TestCase):
    def test_installed_x3ap_matches_all_four_sites(self):
        if not probe.DEFAULT_EXE.is_file():
            self.skipTest('installed X3AP.exe not found')
        report = probe.verify_path(probe.DEFAULT_EXE)
        self.assertEqual(report['result'], 'PASS', report)
        self.assertEqual(
            [item['name'] for item in report['checks']['sites']['items']],
            [site.name for site in probe.SITES])
        self.assertTrue(all(item['whole_instructions']
                            for item in report['checks']['sites']['items']))
        self.assertTrue(all(item['no_interior_branch']
                            for item in report['checks']['sites']['items']))


if __name__ == '__main__':
    unittest.main()
