#!/usr/bin/env python3
"""Executable identity of X3AP.exe: structure plus verified sites; hashes are provenance.

Host mirror of src/proxy/executable_identity.h
(docs/reverse-engineering/executable-identity.md). The identity is the PE
structure (preferred base, i386 PE32, link stamp, entry point, SizeOfImage,
the four-section table, file size), with IMAGE_FILE_LARGE_ADDRESS_AWARE and
the optional-header CheckSum left free, plus one whole-instruction anchor per
engine global the proxy reads. Every hook verifier adds its own site bytes.
The raw SHA-256 is reported against the known list as INFO and never fails a
check. test_exe_identity compares the constants and anchors with the C++ table.

usage: exe_identity.py [--exe PATH] [--objdump PATH]   (exit 0 = PASS)
"""
import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

DEFAULT_EXE = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe'
OBJDUMP = 'i686-w64-mingw32-objdump'

LAA_BIT = 0x0020  # IMAGE_FILE_LARGE_ADDRESS_AWARE in IMAGE_FILE_HEADER.Characteristics
SHIPPED_SHA256 = 'fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab'
LAA_CLEARED_SHA256 = '9d8ddf43f06031f4fb21e5e700d55a2df8881c76bd8c5ce4a9af0866d6dc38c8'
NTCORE_4GB_SHA256 = '5d6b741e7269b40e2a7df850fc274d593d0b39ac647e0b0205b64737c93190c3'
NTCORE_4GB_CHECKSUM = 0x0021bd66
KNOWN_SHA256 = {
    SHIPPED_SHA256: 'shipped (LAA set, CheckSum 0)',
    LAA_CLEARED_SHA256: 'LAA cleared (CheckSum 0)',
    NTCORE_4GB_SHA256: 'NTCore 4gb_patch.exe output (LAA set, CheckSum 0x0021bd66)',
}
# SHA-256 with the LAA bit cleared and the CheckSum field zeroed: the same for
# all three known files (provenance: "the shipped image, whatever the 4GB patch did").
IDENTITY_DIGEST = LAA_CLEARED_SHA256

FILE_SIZE = 2153984
IMAGE_BASE = 0x00400000
IMAGE_SIZE = 0x002f5000
ENTRY_POINT = 0x00112ead
TIME_DATE_STAMP = 0x5a1d70ad
CHARACTERISTICS_WITHOUT_LAA = 0x0103
SECTIONS = (  # name, VirtualSize, VirtualAddress, SizeOfRawData, PointerToRawData, Characteristics
    ('.text', 0x00130630, 0x00001000, 0x00130800, 0x00000400, 0x60000020),
    ('.rdata', 0x0004074d, 0x00132000, 0x00040800, 0x00130c00, 0x40000040),
    ('.data', 0x000efb58, 0x00173000, 0x0000b000, 0x00171400, 0xc0000040),
    ('.rsrc', 0x00091814, 0x00263000, 0x00091a00, 0x0017c400, 0x40000040),
)
# (instruction VA, global VA, prefix, suffix): the instruction bytes are
# prefix + le32(global) + suffix. Same table as executable_identity.h.
ANCHORS = (
    (0x00401b91, 0x0057fc60, b'\x8b\x35', b''),
    (0x00433cbe, 0x00587b88, b'\x89\x15', b''),
    (0x004e21c7, 0x00596928, b'\xd9\x05', b''),
    (0x004e27df, 0x00596934, b'\x89\x35', b''),
    (0x0049959b, 0x00596988, b'\x8b\x2d', b''),
    (0x004995a1, 0x0059698c, b'\xa1', b''),
    (0x0042760b, 0x006069ac, b'\x8b\x0d', b''),
    (0x004275fa, 0x006069b0, b'\x0f\xbf\x05', b''),
    (0x004275f3, 0x006069b4, b'\x0f\xbf\x15', b''),
    (0x00401c19, 0x00606f34, b'\xa1', b''),
    (0x00401bb0, 0x00606f38, b'\x8b\x0d', b''),
    (0x0040258b, 0x00606f3c, b'\xa1', b''),
    (0x004971d3, 0x00606f44, b'\xa1', b''),
    (0x0041efd6, 0x00606fc0, b'\x8b\x35', b''),
    (0x0041305f, 0x00606fd4, b'\x8b\x15', b''),
    (0x004343e6, 0x00607040, b'\x8b\x2d', b''),
    (0x00445a3a, 0x00607ce8, b'\x83\x3d', b'\x00'),
    (0x00401a0d, 0x00608504, b'\x89\x1d', b''),
    (0x00401e69, 0x0060850c, b'\xa1', b''),
    (0x00403367, 0x00608518, b'\xa3', b''),
    (0x0048a6d2, 0x0060851c, b'\xd9\x05', b''),
    (0x004e234b, 0x00608534, b'\x8a\x1d', b''),
    (0x004e29fd, 0x00608538, b'\xa3', b''),
    (0x004e2a24, 0x0060853c, b'\xa3', b''),
    (0x004e246e, 0x00608540, b'\x8b\x0d', b''),
    (0x004e0bff, 0x00608544, b'\x89\x3d', b''),
    (0x004e294c, 0x00608548, b'\xa3', b''),
    (0x004e0c05, 0x0060854c, b'\x89\x3d', b''),
    (0x00401d6b, 0x006085e4, b'\x8b\x0d', b''),
    (0x00412c88, 0x006085f4, b'\x8b\x2d', b''),
    (0x004ae07f, 0x006085f8, b'\x8b\x0d', b''),
    (0x00413ade, 0x006089f8, b'\x01\x1d', b''),
    (0x004e3f9d, 0x006089fc, b'\x8b\x0d', b''),
    (0x004b9a60, 0x00608a38, b'\x89\x35', b''),
    (0x004b9a08, 0x00608a40, b'\x89\x35', b''),
    (0x004b9958, 0x00608a44, b'\x89\x35', b''),
    (0x004b99b0, 0x00608a48, b'\x89\x35', b''),
    (0x00401e0e, 0x00608adc, b'\x89\x35', b''),
    (0x0041d00c, 0x00608dac, b'\x0f\xbf\x0d', b''),
    (0x0041d020, 0x00608db0, b'\x8b\x0d', b''),
    (0x00524fa7, 0x006619ec, b'\xa3', b''),
)


def anchor_bytes(anchor):
    _, global_va, prefix, suffix = anchor
    return prefix + struct.pack('<I', global_va) + suffix


def data_of(source):
    """bytes of a path (str/Path) or the bytes themselves."""
    if isinstance(source, (bytes, bytearray, memoryview)):
        return bytes(source)
    return Path(source).read_bytes()


def nt_offset(data):
    if len(data) < 0x40 or data[:2] != b'MZ':
        raise ValueError('not an MZ image')
    offset = struct.unpack_from('<I', data, 0x3c)[0]
    if not 0 < offset < 0x1000 or offset + 24 + 0x60 > len(data) or data[offset:offset + 4] != b'PE\0\0':
        raise ValueError('no PE header')
    return offset


def characteristics_offset(data):
    return nt_offset(data) + 4 + 18


def checksum_offset(data):
    return nt_offset(data) + 24 + 64


def laa(source):
    """IMAGE_FILE_LARGE_ADDRESS_AWARE as stored in the file."""
    data = data_of(source)
    return bool(struct.unpack_from('<H', data, characteristics_offset(data))[0] & LAA_BIT)


def checksum(source):
    data = data_of(source)
    return struct.unpack_from('<I', data, checksum_offset(data))[0]


def raw_sha256(source):
    return hashlib.sha256(data_of(source)).hexdigest()


def identity_digest(source):
    """SHA-256 with the LAA bit cleared and the CheckSum zeroed (provenance only)."""
    data = bytearray(data_of(source))
    offset = characteristics_offset(data)
    struct.pack_into('<H', data, offset, struct.unpack_from('<H', data, offset)[0] & ~LAA_BIT)
    struct.pack_into('<I', data, checksum_offset(data), 0)
    return hashlib.sha256(data).hexdigest()


def pe_checksum(source):
    """The PE CheckSum as imagehlp MapFileAndCheckSum computes it (field excluded)."""
    data = bytearray(data_of(source))
    struct.pack_into('<I', data, checksum_offset(data), 0)
    length = len(data)
    if length % 2:
        data += b'\0'
    total = 0
    for (word,) in struct.iter_unpack('<H', data):
        total += word
        total = (total & 0xffff) + (total >> 16)
    total = (total & 0xffff) + (total >> 16)
    return (total + length) & 0xffffffff


def with_laa(source, on=True, new_checksum=None):
    """A copy with the LAA bit set (or cleared); CheckSum unchanged unless given."""
    data = bytearray(data_of(source))
    offset = characteristics_offset(data)
    value = struct.unpack_from('<H', data, offset)[0]
    struct.pack_into('<H', data, offset, (value | LAA_BIT) if on else (value & ~LAA_BIT))
    if new_checksum is not None:
        struct.pack_into('<I', data, checksum_offset(data), new_checksum)
    return bytes(data)


def known(sha256):
    return KNOWN_SHA256.get(sha256)


def section_table(data):
    offset = nt_offset(data)
    count, optional = struct.unpack_from('<H', data, offset + 6)[0], struct.unpack_from('<H', data, offset + 20)[0]
    table = []
    for i in range(count):
        s = offset + 24 + optional + 40 * i
        name = data[s:s + 8].rstrip(b'\0').decode('ascii', 'replace')
        virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from('<IIII', data, s + 8)
        table.append((name, virtual_size, virtual_address, raw_size, raw_pointer, struct.unpack_from('<I', data, s + 36)[0]))
    return table


def structure(source):
    """Named structural checks (bool each); the LAA bit and CheckSum are not compared."""
    data = data_of(source)
    checks = {'size': len(data) == FILE_SIZE}
    try:
        offset = nt_offset(data)
        machine, count, stamp = struct.unpack_from('<HHI', data, offset + 4)
        optional_size, characteristics = struct.unpack_from('<HH', data, offset + 20)
        o = offset + 24
        magic = struct.unpack_from('<H', data, o)[0]
        entry, base, image_size = (struct.unpack_from('<I', data, o + 16)[0], struct.unpack_from('<I', data, o + 28)[0],
                                   struct.unpack_from('<I', data, o + 56)[0])
        checks.update({
            'pe32_i386': machine == 0x14c and magic == 0x10b and optional_size == 0xe0,
            'preferred_base': base == IMAGE_BASE,
            'time_date_stamp': stamp == TIME_DATE_STAMP,
            'characteristics': characteristics & ~LAA_BIT == CHARACTERISTICS_WITHOUT_LAA,
            'image_size': image_size == IMAGE_SIZE,
            'entry_point': entry == ENTRY_POINT,
            'sections': count == len(SECTIONS) and tuple(section_table(data)) == SECTIONS,
        })
    except (ValueError, struct.error):
        checks['pe32_i386'] = False
    return checks


def structure_ok(source):
    return all(structure(source).values())


def read_va(data, va, n):
    for name, virtual_size, virtual_address, raw_size, raw_pointer, _ in section_table(data):
        start = IMAGE_BASE + virtual_address
        if start <= va and va + n <= start + min(virtual_size, raw_size):
            return data[raw_pointer + va - start:raw_pointer + va - start + n]
    return None


def anchors(source):
    """{anchor VA: bytes present}."""
    data = data_of(source)
    result = {}
    for anchor in ANCHORS:
        expected = anchor_bytes(anchor)
        try:
            result[anchor[0]] = read_va(data, anchor[0], len(expected)) == expected
        except (ValueError, struct.error):
            result[anchor[0]] = False
    return result


def anchors_ok(source):
    return all(anchors(source).values())


def identity_ok(source):
    """The gate executable_verified() applies: structure and every anchor."""
    data = data_of(source)
    return structure_ok(data) and anchors_ok(data)


def info(source):
    """INFO record for reports: raw hash against the known list, LAA, CheckSum. Never a failure."""
    data = data_of(source)
    sha = raw_sha256(data)
    record = {'sha256': sha, 'known': known(sha), 'bytes': len(data)}
    try:
        record.update(laa=laa(data), checksum=f'{checksum(data):#010x}', identity_digest=identity_digest(data))
        record['known_image'] = record['identity_digest'] == IDENTITY_DIGEST
    except (ValueError, struct.error):
        record.update(laa=None, checksum=None, identity_digest=None, known_image=False)
    return record


def instruction_starts(exe, objdump, vas):
    """Which of vas start an instruction of objdump's linear sweep of .text."""
    wanted = {f'{va:x}' for va in vas}
    found = set()
    with subprocess.Popen([objdump, '-d', '-j', '.text', str(exe)], stdout=subprocess.PIPE, text=True) as process:
        for line in process.stdout:
            head = line.split(':', 1)[0].strip()
            if head in wanted and '\t' in line:
                found.add(int(head, 16))
    return found


def verify(exe, objdump=None):
    data = data_of(exe)
    report = {'exe_info': info(data), 'checks': {}}
    checks = report['checks']
    checks['structure'] = structure(data)
    checks['anchors'] = {f'{va:#010x}': ok for va, ok in anchors(data).items()}
    if objdump:
        starts = instruction_starts(exe, objdump, [a[0] for a in ANCHORS])
        checks['anchor_boundaries'] = {f'{a[0]:#010x}': a[0] in starts for a in ANCHORS}
    report['result'] = 'PASS' if all(all(group.values()) for group in checks.values()) else 'FAIL'
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE)
    parser.add_argument('--objdump', default=OBJDUMP, help='whole-instruction check of every anchor ("" skips it)')
    parser.add_argument('--json', action='store_true', help='print every check')
    args = parser.parse_args()
    if not args.exe.is_file():
        print(json.dumps({'result': 'SKIP', 'reason': f'{args.exe} not found'}))
        return 2
    objdump = shutil.which(args.objdump) if args.objdump else None
    report = verify(args.exe, objdump)
    report['exe'] = str(args.exe)
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        summary = {name: all(group.values()) for name, group in report['checks'].items()}
        print(json.dumps({'result': report['result'], 'exe_info': report['exe_info'], 'anchors': len(ANCHORS), **summary}))
    return 0 if report['result'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
