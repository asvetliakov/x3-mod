#!/usr/bin/env python3
"""Every .text instruction that names one of the SETA globals as an absolute operand, with its distance to the
nearest claimed site (verify_collide_sites.other_claims, every module) and to the nearest .text literal under
src/proxy; the chosen anchors (exe_identity.ANCHORS) are marked and re-checked (bytes and instruction start).

usage: seta_anchor_candidates.py [--exe PATH]   (exit 0 = every chosen anchor verified, no overlap)
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import exe_identity as ident  # noqa: E402
import verify_collide_sites as collector  # noqa: E402

GLOBALS = (0x0057847c, 0x00578488, 0x0057fc68, 0x0057fe74, 0x00595ee8, 0x0060903c, 0x00608e40, 0x00608e44, 0x00608e48,
           0x00608e4c, 0x00608e50, 0x00608e78, 0x00608ea0)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, default=ident.DEFAULT_EXE)
    args = parser.parse_args()
    data = args.exe.read_bytes()
    spans = [(a, a + n, name) for name, a, n in collector.other_claims(skip_prefix='\0', own_names=())]
    text_lo, text_hi = ident.IMAGE_BASE + ident.SECTIONS[0][2], ident.IMAGE_BASE + ident.SECTIONS[0][2] + ident.SECTIONS[0][1]
    literals = sorted({int(v, 16) for f in (ROOT / 'src/proxy').iterdir() if f.suffix in ('.h', '.cpp') and f.name != 'executable_identity.h'
                       for v in re.findall(r'(?<![0-9A-Za-z_])0x([0-9a-fA-F]{6,8})\b', f.read_text(errors='replace'))
                       if text_lo <= int(v, 16) < text_hi})
    chosen = {a[1]: a for a in ident.ANCHORS if a[1] in GLOBALS}
    wanted = {f'0x{g:x}': g for g in GLOBALS}
    lines = subprocess.run([ident.OBJDUMP, '-d', '-j', '.text', str(args.exe)], capture_output=True, text=True).stdout.splitlines()
    ok = len(chosen) == len(GLOBALS)
    for line in lines:
        hits = [g for s, g in wanted.items() if re.search(re.escape(s) + r'\b', line)]
        if not hits or '\t' not in line:
            continue
        va, length = int(line.split(':')[0], 16), len(line.split('\t')[1].split())
        overlap = [n for lo, hi, n in spans if lo < va + length and va < hi]
        literal = min(literals, key=lambda v: abs(v - va))
        claim = min(spans, key=lambda s: min(abs(s[0] - va), abs(s[1] - va)))
        anchor = chosen.get(hits[0])
        mark = ''
        if anchor and anchor[0] == va:
            verified = ident.read_va(data, va, len(ident.anchor_bytes(anchor))) == ident.anchor_bytes(anchor)
            ok &= verified and not overlap
            mark = f' ANCHOR bytes_ok={int(verified)}'
        print(f'{hits[0]:#010x} {va:#010x} len={length} overlap={overlap} literal_d={literal - va:+d} '
              f'claim={claim[2]} claim_d={claim[0] - va:+d}{mark}')
    print('result', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
