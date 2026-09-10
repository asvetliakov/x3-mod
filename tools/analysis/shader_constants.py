#!/usr/bin/env python3
"""Read D3D9 CTAB metadata from locally captured shader bytecode.

Offsets in a CTAB are relative to its 28-byte constant-table header, immediately
after the FourCC. This exports names/register metadata only, never shader code.
No semantic assumptions (e.g. matrix convention) are made from names alone.
"""
import argparse
import json
from pathlib import Path
import struct


def parse_ctab(code):
    def uint(offset):
        return struct.unpack_from('<I', code, offset)[0]
    pos = 4  # skip shader version
    while pos + 4 <= len(code):
        token = uint(pos)
        if token == 0x0000ffff:
            return []
        if token & 0xffff == 0xfffe:
            count = (token >> 16) & 0x7fff
            end = pos + 4 + count*4
            if end > len(code):
                raise ValueError('Truncated shader comment')
            if count and code[pos+4:pos+8] == b'CTAB':
                table = code[pos+8:end]
                if len(table) < 28:
                    raise ValueError('Truncated CTAB header')
                size, creator, version, total, offset, flags, target = struct.unpack_from('<7I', table)
                if size != 28 or offset + total*20 > len(table):
                    raise ValueError('Invalid CTAB constant range')
                def string(start):
                    if start >= len(table):
                        raise ValueError('CTAB string offset out of range')
                    stop = table.find(b'\0', start)
                    if stop < 0:
                        raise ValueError('Unterminated CTAB string')
                    return table[start:stop].decode('utf-8', errors='replace')
                type_budget = [4096]
                def parameter_type(start, ancestors=frozenset()):
                    # CTAB type records can share subtrees. Validate cycles and
                    # bound expansion instead of trusting arbitrary nested offsets.
                    if start in ancestors or len(ancestors) >= 16:
                        raise ValueError('Cyclic or excessively nested CTAB type')
                    type_budget[0] -= 1
                    if type_budget[0] < 0 or start + 16 > len(table):
                        raise ValueError('CTAB type offset or expansion out of range')
                    cls, typ, rows, cols, elems, members, memberoffset = struct.unpack_from('<6HI', table, start)
                    result = dict(parameter_class=cls, parameter_type=typ, rows=rows,
                                  columns=cols, elements=elems, struct_members=members)
                    if members:
                        if memberoffset + members*8 > len(table):
                            raise ValueError('CTAB member range out of bounds')
                        result['members'] = []
                        for member in range(members):
                            nameoffset, child = struct.unpack_from('<2I', table, memberoffset+member*8)
                            result['members'].append(dict(name=string(nameoffset),
                                **parameter_type(child, ancestors | {start})))
                    return result
                results = []
                for i in range(total):
                    name, regset, reg, regs, reserved, typeinfo, default = struct.unpack_from('<I4H2I', table, offset+i*20)
                    results.append(dict(name=string(name), register_set=regset, register=reg, count=regs,
                                        **parameter_type(typeinfo)))
                return results
            pos = end
        else:
            # CTAB metadata normally precedes instructions. SM2/3 encode length.
            if ((uint(0) >> 8) & 255) < 2:
                return []
            pos += 4 * (1 + ((token >> 24) & 15))
    return []


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = {}
    for file in sorted(args.directory.glob('*.bin')):
        if file.name.startswith(('ps_', 'vs_')):
            result[file.stem] = parse_ctab(file.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(f'Indexed constants for {len(result)} shaders -> {args.output}')


if __name__ == '__main__':
    main()
