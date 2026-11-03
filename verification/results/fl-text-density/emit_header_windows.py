#!/usr/bin/env python3
"""Prints the identity-window table of src/engine/fl/text_density_sites_fl.h from X3FL.exe (read-only).

The 22 windows of verify_text_density_sites_fl.py (WINDOW_SPANS, verified 2026-11-03) plus the nine the FL module
adds (EXTRA below: the language field's default store, the two blits' destination clip with the FL blit recorder
test, the image case's object/path slots, the texture creator's generated-row surface flags, the object's +0x34
texture store, the KC VM slot store, the Materials row-count store). Bytes with every HIGHLOW dword zeroed, FNV-1a of
those bytes, each fixup's offset and target RVA. With --check the header's arrays are compared instead (exit 0 = equal).

usage: python3 -I verification/results/fl-text-density/emit_header_windows.py [--exe PATH] [--check]
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/probe'))
from verify_chase_camera_site import FL_DEFAULT_EXE, FlImage, fnv1a32  # noqa: E402
from verify_text_density_sites_fl import WINDOW_SPANS, window_record  # noqa: E402

EXTRA = (
    ('config_language', 0x109ec4, 0x0a),   # mov dword [edi+0x75c],44: the language field (font-selector-readiness.md)
    ('blt_block_clip', 0x0a7d88, 0xbd),    # clip argument test .. destination clip .. recorder test (*0x1aa100)+0x24
    ('blt_alpha_clip', 0x0a81d8, 0xbd),
    ('image_case', 0x0b21f8, 0xc8),        # path slot 0x1aa0a4, object slot 0x1aa070, [ebp-0x50] src, [ebp-0x20] dst
    ('creator_flags', 0x111499, 0x1e),     # generated row: surface flags 0x402c
    ('object_texture', 0x0f59c7, 0x03),    # mov [ebx+0x34],eax: the object's IDirect3DTexture9
    ('vm_store', 0x077561, 0x05),          # mov [0x1aa174],eax: the KC VM slot (init routine, after Direct3DCreate9)
    ('rows_store', 0x111755, 0x05),        # mov [0x21b410],eax: the Materials row count (the loader's only writer)
)
SPANS = tuple(WINDOW_SPANS) + EXTRA


def records(img):
    out = []
    for name, rva, length in SPANS:
        rec = window_record(img, rva, length)
        if rec is None:
            raise SystemExit(f'{name}: a fixup straddles the window edge')
        out.append((name, rva, length, rec[0], rec[1]))
    return out


def emit(recs):
    lines = []
    for name, rva, length, raw, fix in recs:
        rows = [', '.join(f'0x{b:02x}' for b in raw[k:k + 18]) for k in range(0, len(raw), 18)]
        body = ',\n    '.join(rows)
        lines.append(f'inline constexpr unsigned char {name}_window[{length}] = {{\n    {body}}};')
    lines.append(f'constexpr unsigned window_count = {len(recs)}, max_window = {max(r[2] for r in recs)};')
    lines.append('constexpr Window windows[window_count] = {')
    for name, rva, length, raw, fix in recs:
        offs = ', '.join(f'0x{o:02x}' for o, _ in fix) or '0'
        tgts = ', '.join(f'0x{t:06x}' for _, t in fix) or '0'
        lines.append(f'    {{"{name}", 0x{rva:06x}, {length}, {name}_window, 0x{fnv1a32(raw):08x}u, {len(fix)}, {{{offs}}},\n     {{{tgts}}}}},')
    lines.append('};')
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, default=FL_DEFAULT_EXE)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    img = FlImage(args.exe.read_bytes())
    text = emit(records(img))
    if not args.check:
        print(text)
        return 0
    header = (ROOT / 'src/engine/fl/text_density_sites_fl.h').read_text()
    squash = lambda s: re.sub(r'\s+', '', s)  # noqa: E731
    ok = squash(text) in squash(header)
    print(f'header windows {"match" if ok else "DIFFER"} ({len(SPANS)} windows, X3FL.exe {args.exe})')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
