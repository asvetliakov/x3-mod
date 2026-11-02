#!/usr/bin/env python3
"""Read-only census of the d3dx9 imports of X3FL.exe (and X3AP.exe when present) for the material-strength texture
hook on FL (src/proxy/d3dx_texture_hook.cpp; docs/architecture/overlay-material-strength.md, "Farnham's Legacy").

Parses the import directory with tools/analysis/inspect_x3.py's PE reader (standard library only) and prints, per
executable: SHA-256, DllCharacteristics, every d3dx9_NN.dll descriptor with its imported names, its OriginalFirstThunk
and the IAT slot RVA of D3DXCreateTextureFromFileInMemoryEx (the slot the hook exchanges at module base + RVA), and
whether exactly one d3dx9 descriptor imports that name through a name table (the hook's resolution rule). Never writes
the game directory. Usage: python3 -I import_census.py [exe...]  (default: /mnt/c/X3FL/X3FL.exe, /mnt/c/X3/X3AP.exe)
"""
import hashlib
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools/analysis'))
from inspect_x3 import PE32  # noqa: E402

TARGET = 'D3DXCreateTextureFromFileInMemoryEx'
DEFAULT = ['/mnt/c/X3FL/X3FL.exe', '/mnt/c/X3/X3AP.exe']


def census(path):
    data = Path(path).read_bytes()
    pe = PE32(data)
    e_lfanew = pe.u32(0x3c)
    characteristics = struct.unpack_from('<H', data, e_lfanew + 24 + 70)[0]
    print(f'exe={path} sha256={hashlib.sha256(data).hexdigest()} image_base=0x{pe.base:08x} '
          f'dll_characteristics=0x{characteristics:04x} dynamic_base={int(bool(characteristics & 0x40))}')
    hits = []
    pos = pe.offset(pe.import_rva)
    while any(data[pos:pos + 20]):
        lookup, _, _, name, iat = struct.unpack_from('<IIIII', data, pos)
        dll = pe.string(pe.offset(name))
        pos += 20
        if not re.fullmatch(r'd3dx9_\d+\.dll', dll, re.I):
            continue
        names = []
        index = 0
        thunk = pe.offset(lookup or iat)
        while pe.u32(thunk + index * 4):
            value = pe.u32(thunk + index * 4)
            symbol = f'ordinal:{value & 0xffff}' if value & 0x80000000 else pe.string(pe.offset(value) + 2)
            names.append(symbol)
            if symbol == TARGET:
                hits.append((dll, iat + index * 4, bool(lookup)))
            index += 1
        print(f'  descriptor dll={dll} original_first_thunk=0x{lookup:08x} first_thunk=0x{iat:08x} imports={len(names)}')
        for symbol in names:
            print(f'    {symbol}')
    for dll, slot, named in hits:
        print(f'  target={TARGET} dll={dll} slot_rva=0x{slot:08x} by_name_table={int(named)}')
    print(f'  resolvable={int(len(hits) == 1 and hits[0][2])} matches={len(hits)}')


def main():
    for path in sys.argv[1:] or DEFAULT:
        if Path(path).exists():
            census(path)
        else:
            print(f'exe={path} absent')


if __name__ == '__main__':
    main()
