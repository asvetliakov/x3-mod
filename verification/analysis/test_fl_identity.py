"""Host tests of the X3FL.exe identity (src/engine/fl/executable_identity_fl.h).

docs/architecture/farnhams-legacy-build.md. The generated constants
(executable_identity_fl_inc.h) are parsed back from the C++ fragment and applied by a Python
mirror of structure_mismatch() plus the file-size check, field for field in the C++ order, to
the PE headers of the files on disk: X3FL.exe verifies, X3AP.exe fails at the link stamp, the
LAA bit is free, and a short SizeOfHeaders, a changed section, entry anchor byte or file size fails
with that reason.
The fragment must equal the generator's output for the installed X3FL.exe. The executables are
only read; mutations are made on in-memory copies. The C++ template itself runs only in the
FL DLL (not here).
"""
import importlib.util
from pathlib import Path
import re
import struct
import unittest

ROOT = Path(__file__).resolve().parents[2]
FL_EXE = Path('/mnt/c/X3FL/X3FL.exe')
AP_EXE = Path('/mnt/c/X3/X3AP.exe')
HEADER = ROOT / 'src/engine/fl/executable_identity_fl.h'
FRAGMENT = ROOT / 'src/engine/fl/executable_identity_fl_inc.h'
LAA_BIT = 0x0020
# The C++ reasons in check order (dos_header .. entry_anchor in structure_mismatch, then file_size).
REASONS = ['dos_header', 'nt_header', 'machine', 'timestamp', 'image_size', 'entry_point', 'characteristics',
           'section_count', 'headers', 'section_table', 'entry_anchor', 'file_size']

spec = importlib.util.spec_from_file_location('fl_identity_constants', ROOT / 'tools/analysis/fl_identity_constants.py')
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)


def constants():
    """The values of the generated C++ fragment."""
    text = FRAGMENT.read_text()
    num = lambda name: int(re.search(rf'\b{name} = (0x[0-9a-f]+|\d+)', text).group(1), 0)
    sections = []
    for m in re.finditer(r"\{\{([^}]*)\}, (0x[0-9a-f]+), (0x[0-9a-f]+), (0x[0-9a-f]+), (0x[0-9a-f]+), (0x[0-9a-f]+)\}", text):
        name = bytes(ord(c[1]) if c.startswith("'") else 0 for c in m.group(1).split(','))
        sections.append((name, *(int(m.group(i), 16) for i in range(2, 7))))
    anchor = bytes(int(b, 16) for b in re.search(r'entry_anchor\[\d+\] = \{([^}]*)\}', text).group(1).split(','))
    return {'image_size': num('image_size'), 'entry_point': num('entry_point'), 'time_date_stamp': num('time_date_stamp'),
            'characteristics_without_laa': num('characteristics_without_laa'), 'file_size': num('file_size'),
            'sections': sections, 'entry_anchor_rva': num('entry_anchor_rva'), 'entry_anchor': anchor}


def mismatch(data, c):
    """Python mirror of structure_mismatch() + known_file_size(): None or the first failing field."""
    if data[:2] != b'MZ':
        return 'dos_header'
    lfanew = struct.unpack_from('<i', data, 0x3c)[0]
    if lfanew <= 0 or lfanew >= 0x1000:
        return 'dos_header'
    if data[lfanew:lfanew + 4] != b'PE\0\0':
        return 'nt_header'
    f = gen.parse(data)
    if f['machine'] != 0x14c or f['optional_header_size'] != 0xe0 or f['magic'] != 0x10b:
        return 'machine'
    if f['time_date_stamp'] != c['time_date_stamp']:
        return 'timestamp'
    if f['image_size'] != c['image_size']:
        return 'image_size'
    if f['entry_point'] != c['entry_point']:
        return 'entry_point'
    if f['characteristics'] & ~LAA_BIT & 0xffff != c['characteristics_without_laa']:
        return 'characteristics'
    if len(f['sections']) != len(c['sections']):
        return 'section_count'
    if f['size_of_headers'] < f['nt_offset'] + 0xf8 + 40 * len(c['sections']):
        return 'headers'
    if f['sections'] != c['sections']:
        return 'section_table'
    off = gen.rva_to_offset(f, c['entry_anchor_rva'])
    if data[off:off + len(c['entry_anchor'])] != c['entry_anchor']:
        return 'entry_anchor'
    if len(data) != c['file_size']:
        return 'file_size'
    return None


def need(path):
    if not path.exists():
        raise unittest.SkipTest(f'{path} absent')
    return path.read_bytes()


class FlIdentity(unittest.TestCase):
    def test_fragment_is_generated_from_x3fl(self):
        data = need(FL_EXE)
        self.assertEqual(FRAGMENT.read_text(), gen.fragment(data, gen.parse(data)))

    def test_x3fl_verifies(self):
        data = need(FL_EXE)
        c = constants()
        self.assertIsNone(mismatch(data, c))
        self.assertEqual((c['time_date_stamp'], c['file_size'], c['entry_point']), (0x618416c8, 2804736, 0x11ea9d))
        self.assertEqual([s[0].rstrip(b'\0') for s in c['sections']],
                         [b'.text', b'.rdata', b'.data', b'.gfids', b'.tls', b'_RDATA', b'.rsrc', b'.reloc'])

    def test_x3ap_mismatches(self):
        self.assertEqual(mismatch(need(AP_EXE), constants()), 'timestamp')

    def test_mutations(self):
        data = need(FL_EXE)
        c = constants()
        f = gen.parse(data)
        nt = struct.unpack_from('<I', data, 0x3c)[0]
        laa = bytearray(data)
        laa[nt + 22] ^= LAA_BIT  # FileHeader.Characteristics, low byte
        self.assertIsNone(mismatch(bytes(laa), c))
        section = bytearray(data)
        section[nt + 24 + f['optional_header_size'] + 36] ^= 0x01  # first section's Characteristics
        self.assertEqual(mismatch(bytes(section), c), 'section_table')
        anchor = bytearray(data)
        anchor[gen.rva_to_offset(f, c['entry_anchor_rva'])] ^= 0xff
        self.assertEqual(mismatch(bytes(anchor), c), 'entry_anchor')
        self.assertEqual(mismatch(data + b'\0', c), 'file_size')
        headers = bytearray(data)
        struct.pack_into('<I', headers, nt + 24 + 60, nt + 0xf8 + 40 * len(c['sections']) - 1)  # SizeOfHeaders
        self.assertEqual(mismatch(bytes(headers), c), 'headers')
        self.assertGreaterEqual(f['size_of_headers'], nt + 0xf8 + 40 * len(c['sections']))

    def test_cxx_reasons_and_rva_contract(self):
        header = HEADER.read_text()
        order = re.findall(r'return "(\w+)";', header)
        seen = list(dict.fromkeys(order))
        self.assertEqual(seen + ['file_size'], REASONS)
        self.assertIn('base + entry_anchor_rva', header)
        self.assertNotIn('0x00400000', header)
        self.assertNotIn('ImageBase', re.sub(r'//.*', '', header))  # the relocated header's ImageBase is never compared
        self.assertIn('"file_size"', (ROOT / 'src/engine/fl/executable_identity_fl.cpp').read_text())

    def test_edition_wiring(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertIn('set(X3M_EDITION "ap" CACHE STRING', cmake)
        self.assertIn('src/engine/fl/executable_identity_fl.cpp', cmake)
        trace = (ROOT / 'src/proxy/object_trace.h').read_text()
        self.assertIn('#if X3M_EDITION_FL\n', trace)
        self.assertIn('constexpr bool executable_verified() { return false; }', trace)
        loader = (ROOT / 'src/proxy/loader.cpp').read_text()
        self.assertIn('"fl.identity ok base=0x%08lx timestamp=0x%08lx image_size=0x%08lx"', loader)
        self.assertIn('"fl.identity mismatch reason=%s"', loader)


if __name__ == '__main__':
    unittest.main()
