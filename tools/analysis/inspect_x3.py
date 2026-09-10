#!/usr/bin/env python3
"""Read-only metadata inventory of a local X3 installation; never extract assets.

Uses only Python's standard library. Output includes executable hashes, PE imports,
render-related string locations, and catalogue entry offsets/sizes. Addresses are
preferred virtual addresses, not portable hooks. CAT decoding is validated against
the paired DAT length, and does not infer the engine's override precedence.
"""
import argparse
import hashlib
import json
import re
import struct
from collections import Counter
from pathlib import Path


class PE32:
    """Minimal PE32 metadata reader with RVA-to-file-offset translation."""
    def __init__(self, data):
        self.data = data
        if data[:2] != b'MZ':
            raise ValueError('Not a DOS/PE executable')
        pe = self.u32(0x3c)
        if data[pe:pe + 4] != b'PE\0\0':
            raise ValueError('Missing PE signature')
        self.machine, count = struct.unpack_from('<HH', data, pe + 4)
        optional = pe + 24
        if struct.unpack_from('<H', data, optional)[0] != 0x10b:
            raise ValueError('Only PE32 is supported')
        self.base = self.u32(optional + 28)
        self.entry = self.base + self.u32(optional + 16)
        self.import_rva = self.u32(optional + 104)
        self.sections = []
        table = optional + struct.unpack_from('<H', data, pe + 20)[0]
        for index in range(count):
            pos = table + index * 40
            size, rva, raw_size, offset = struct.unpack_from('<IIII', data, pos + 8)
            self.sections.append(dict(name=data[pos:pos + 8].rstrip(b'\0').decode(),
                                      size=size, rva=rva, raw_size=raw_size, offset=offset))

    def u32(self, pos):
        return struct.unpack_from('<I', self.data, pos)[0]

    def offset(self, rva):
        for section in self.sections:
            if section['rva'] <= rva < section['rva'] + section['raw_size']:
                return section['offset'] + rva - section['rva']
        raise ValueError(f'RVA not file-backed: {rva:x}')

    def va(self, offset):
        for section in self.sections:
            if section['offset'] <= offset < section['offset'] + section['raw_size']:
                return self.base + section['rva'] + offset - section['offset']
        return None

    def string(self, offset):
        return self.data[offset:self.data.index(b'\0', offset)].decode('ascii')

    def imports(self):
        result = {}
        if not self.import_rva:
            return result
        pos = self.offset(self.import_rva)
        while any(self.data[pos:pos + 20]):
            lookup, _, _, name, iat = struct.unpack_from('<IIIII', self.data, pos)
            dll = self.string(self.offset(name))
            thunk = self.offset(lookup or iat)
            values = []
            index = 0
            while self.u32(thunk + index * 4):
                value = self.u32(thunk + index * 4)
                symbol = f'ordinal:{value & 0xffff}' if value & 0x80000000 else self.string(self.offset(value) + 2)
                values.append(dict(name=symbol, iat_va=f'0x{self.base + iat + index * 4:08x}'))
                index += 1
            result[dll] = values
            pos += 20
        return result


def read_catalogue(cat):
    """Decode CAT directory, validating lengths without touching DAT contents."""
    raw = cat.read_bytes()
    lines = bytes(value ^ ((0xdb + index) & 255) for index, value in enumerate(raw)).decode('utf-8').splitlines()
    if not lines or Path(lines[0]).name != lines[0] or not lines[0].endswith('.dat'):
        raise ValueError(f'Unexpected DAT filename in {cat}')
    entries = []
    offset = 0
    for line in lines[1:]:
        path, length = line.rsplit(' ', 1)
        size = int(length)
        if size < 0:
            raise ValueError(f'Negative size in {cat}')
        entries.append(dict(path=path, size=size, offset=offset))
        offset += size
    # Installed archives may retain stale directory header names (e.g. foo.dat).
    # The paired same-stem DAT is the observed matching file.
    dat = cat.with_suffix('.dat')
    if offset != dat.stat().st_size:
        raise ValueError(f'Catalogue sizes ({offset}) do not match {dat}')
    return entries


def inspect(root):
    exe = root / 'X3AP.exe'
    raw = exe.read_bytes()
    pe = PE32(raw)
    # Parameter names and specific render paths; omit generic error-message tables.
    pattern = re.compile(r'^(g_|t_|LightDir_|ViewPortSize$|RenderColorTarget|shader\\|Video.*(Shader|Antialias|D3D)|bloom$|z_only$|standard_lighting$)')
    strings = [dict(text=m.group().decode(), file_offset=f'0x{m.start():x}', va=f'0x{pe.va(m.start()):08x}')
               for m in re.finditer(rb'[ -~]{4,}', raw)
               if pe.va(m.start()) is not None and pattern.search(m.group().decode())]
    catalogues = []
    for cat in sorted([*root.glob('[0-9][0-9].cat'), *root.glob('addon/[0-9][0-9].cat')]):
        entries = read_catalogue(cat)
        shaders = [entry for entry in entries if entry['path'].startswith('shader/')]
        catalogues.append(dict(path=str(cat.relative_to(root)), entries=len(entries),
                               shader_count=len(shaders),
                               shader_profiles=dict(Counter(entry['path'].split('/')[1] for entry in shaders)),
                               shaders=shaders))
    return dict(executable=dict(name=exe.name, sha256=hashlib.sha256(raw).hexdigest(),
                               bytes=len(raw), machine=hex(pe.machine), image_base=hex(pe.base),
                               entry_point=hex(pe.entry), sections=pe.sections, imports=pe.imports(),
                               render_strings=strings), catalogues=catalogues)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path, help='Directory containing X3AP.exe')
    parser.add_argument('--output', type=Path, help='JSON metadata destination; stdout if omitted')
    args = parser.parse_args()
    result = json.dumps(inspect(args.game), indent=2) + '\n'
    if args.output:
        args.output.write_text(result)
    else:
        print(result, end='')


if __name__ == '__main__':
    main()
