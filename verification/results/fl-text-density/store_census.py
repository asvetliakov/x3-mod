#!/usr/bin/env python3
"""Direct-store census of the two cells the FL text density uses as its order witness (read-only, X3FL.exe).

For each occurrence of the absolute address inside .text, the instruction covering it is decoded (Capstone, trying the
1..3 bytes before the dword as its start) and kept when that address is the instruction's destination operand. Prints
the writers of the KC VM slot (RVA 0x1aa174) and of the Materials row count (RVA 0x21b410), and the .data raw size (the
cells past it are zero-filled at load). Indirect writes are not covered (the cells are module globals written by their
owners through the absolute form). docs/reverse-engineering/fl/text-density.md, "Learned while porting".

usage: python3 -I verification/results/fl-text-density/store_census.py [--exe PATH]
"""
import argparse
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/probe'))
from verify_chase_camera_site import FL_DEFAULT_EXE, FlImage  # noqa: E402

CELLS = (('vm_slot', 0x1aa174), ('rows_count', 0x21b410))


def writers(img, text_rva, text, va):
    pattern, hits, i = struct.pack('<I', va), [], 0
    while (i := text.find(pattern, i)) >= 0:
        rva = text_rva + i
        for back in range(1, 4):
            insn = next(img.md.disasm(img.rva(rva - back, 16), img.image_base + rva - back), None)
            if insn and insn.size >= back + 4:
                destination = insn.op_str.split(',')[0]
                if f'[{hex(va)}]' in destination and insn.mnemonic.startswith('mov'):
                    hits.append(f'0x{rva - back:06x} {insn.mnemonic} {insn.op_str}')
                break
        i += 1
    return hits


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, default=FL_DEFAULT_EXE)
    args = parser.parse_args()
    img = FlImage(args.exe.read_bytes())
    name, base, vsize, raw, rsize = next(s for s in img.sections if s[0] == '.text')
    text = img.read(base, vsize)
    for cell, rva in CELLS:
        print(cell, f'0x{rva:06x}', writers(img, base - img.image_base, text, img.image_base + rva))
    data = next(s for s in img.sections if s[0] == '.data')
    print('.data', f'rva=0x{data[1] - img.image_base:06x}', f'raw_size=0x{data[4]:x}', f'virtual_size=0x{data[2]:x}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
