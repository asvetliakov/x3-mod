"""Synthetic verification fixtures; no game files or bytecode are redistributed."""
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
from inspect_x3 import PE32, read_catalogue
from index_shaders import embedded_shaders, fnv1a64


def tokens(*words):
    return struct.pack('<' + 'I' * len(words), *words)


class ShaderParsingTests(unittest.TestCase):
    def test_fnv_public_test_vectors(self):
        self.assertEqual(fnv1a64(b''), 'cbf29ce484222325')
        self.assertEqual(fnv1a64(b'hello'), 'a430d84680aabd0b')

    def test_comment_end_marker_does_not_truncate_shader(self):
        stream = tokens(0xffff0300, 0x0002fffe, 0x0000ffff, 0x12345678,
                        0x02000001, 0x800f0000, 0x90e40000, 0x0000ffff)
        found = list(embedded_shaders(tokens(0x11111111) + stream + tokens(0)))
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0]['offset'], 4)
        self.assertEqual(found[0]['bytes'], len(stream))
        self.assertEqual(found[0]['fnv1a64'], fnv1a64(stream))

    def test_truncated_stream_rejected(self):
        self.assertEqual(list(embedded_shaders(tokens(0xffff0300, 0x02000001, 0x800f0000))), [])

    def test_sm1_def_literal_end_marker_is_not_end(self):
        # DEF has five operands, including four arbitrary immediate float words.
        stream = tokens(0xffff0101, 81, 0xa00f0000, 0xffff, 0, 0, 0, 0xffff)
        found = list(embedded_shaders(stream))
        self.assertEqual(found[0]['bytes'], len(stream))

    def test_adjacent_streams_exclude_container_padding(self):
        stream = tokens(0xffff0200, 0xffff)
        found = list(embedded_shaders(stream + tokens(0x11111111) + stream))
        self.assertEqual([s['offset'] for s in found], [0, 12])
        self.assertEqual(found[0]['fnv1a64'], found[1]['fnv1a64'])

    def test_extended_sm2_both_stages_preserve_comments_and_instruction_lengths(self):
        for version, stage in ((0xffff0201, 'ps'), (0xfffe0201, 'vs')):
            stream = tokens(version, 0x0002fffe, 0x0000ffff, version,
                            0x02000001, 0x800f0000, 0x90e40000, 0x0000ffff)
            found = list(embedded_shaders(stream))
            self.assertEqual(len(found), 1)
            self.assertEqual((found[0]['stage'], found[0]['model']), (stage, '2_1'))
            self.assertEqual(found[0]['bytes'], len(stream))
            self.assertEqual(found[0]['fnv1a64'], fnv1a64(stream))

    def test_unsupported_extended_versions_and_truncated_sm2_are_rejected(self):
        for version in (0xffff0202, 0xfffe0202, 0xffff0301, 0xfffe0301):
            self.assertEqual(list(embedded_shaders(tokens(version, 0x0000ffff))), [])
        self.assertEqual(list(embedded_shaders(tokens(0xffff0201, 0x02000001, 0x800f0000))), [])


class CatalogueTests(unittest.TestCase):
    def test_stale_header_and_spaces_in_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            cat = Path(directory) / '05.cat'
            decoded = b'foo.dat\nshader/path with spaces.fb 3\nsecond 2\n'
            cat.write_bytes(bytes(v ^ ((0xdb+i) & 255) for i, v in enumerate(decoded)))
            cat.with_suffix('.dat').write_bytes(b'12345')
            entries = read_catalogue(cat)
            self.assertEqual(entries[0]['path'], 'shader/path with spaces.fb')
            self.assertEqual(entries[1]['offset'], 3)
            cat.with_suffix('.dat').write_bytes(b'1234')
            with self.assertRaises(ValueError):
                read_catalogue(cat)

    def test_invalid_pe_rejected(self):
        with self.assertRaises(ValueError):
            PE32(b'not an executable')


if __name__ == '__main__':
    unittest.main()
