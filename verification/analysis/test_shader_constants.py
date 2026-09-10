"""CTAB offset/register tests using a synthetic constant table."""
import sys
from pathlib import Path
import struct
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/analysis'))
from shader_constants import parse_ctab


def shader(table):
    payload = b'CTAB' + table
    payload += b'\0' * (-len(payload) % 4)
    return struct.pack('<II', 0xfffe0300, ((len(payload)//4) << 16) | 0xfffe) + payload + struct.pack('<I', 0xffff)


class ConstantTests(unittest.TestCase):
    def test_offsets_relative_to_header_and_register_namespace(self):
        table = struct.pack('<7I',28,0,0xfffe0300,1,28,0,0)
        table += struct.pack('<I4H2I',64,1,0,1,0,48,0)
        table += struct.pack('<6HI',0,2,1,1,1,0,0)
        table += b'g_nNumLightPoint\0'
        result = parse_ctab(shader(table))
        self.assertEqual(result[0]['name'], 'g_nNumLightPoint')
        self.assertEqual(result[0]['register_set'], 1)
        self.assertEqual(result[0]['register'], 0)

    def test_truncated_comment_rejected(self):
        with self.assertRaises(ValueError):
            parse_ctab(struct.pack('<II',0xfffe0300,0x0010fffe)+b'CTAB')

    def test_invalid_constant_range_rejected(self):
        with self.assertRaises(ValueError):
            parse_ctab(shader(struct.pack('<7I',28,0,0xfffe0300,1,200,0,0)))
