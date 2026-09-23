#!/usr/bin/env python3
"""Flow-control and fetch opcodes of embedded ps_3_0 programs, a git revision against the working tree
(docs/architecture/engine-frame-time.md, "TAA stage cost"): shows that the early-outs compiled to real
branches (ifc / breakc / texkill) rather than predication. Host only, no Wine.
Usage: sm3_flow.py [--rev HEAD] temporal_line_mask_camera temporal_line_mask temporal_thin_box_rows"""
import argparse
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
NAMES = {0x1B: 'loop', 0x1D: 'endloop', 0x26: 'rep', 0x27: 'endrep', 0x28: 'if', 0x29: 'ifc', 0x2A: 'else', 0x2B: 'endif',
         0x2C: 'break', 0x2D: 'breakc', 0x41: 'texkill', 0x5F: 'texldl', 0x42: 'texld', 0x5E: 'setp', 0x60: 'breakp'}


def opcodes(text):
    words = [int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{8})u', text)]
    i, out = 1, []  # word 0 is the version token
    while i < len(words):
        token = words[i]
        if token == 0x0000FFFF:
            break
        op = token & 0xFFFF
        if op == 0xFFFE:  # comment block
            i += 1 + ((token >> 16) & 0x7FFF)
            continue
        out.append(op)
        i += 1 + ((token >> 24) & 0x0F)  # SM2+ instruction length
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--rev', default='HEAD')
    parser.add_argument('programs', nargs='+')
    args = parser.parse_args()
    for name in args.programs:
        path = f'src/renderer/{name}_program_inc.h'
        old = subprocess.run(['git', '-C', str(ROOT), 'show', f'{args.rev}:{path}'], capture_output=True, text=True, check=True).stdout
        for label, text in ((args.rev, old), ('tree', (ROOT / path).read_text())):
            seq = [NAMES.get(op, '.') for op in opcodes(text)]
            print(f'{name} {label} texldl={seq.count("texldl")} flow: ' + ' '.join(s for s in seq if s != '.'))


if __name__ == '__main__':
    main()
