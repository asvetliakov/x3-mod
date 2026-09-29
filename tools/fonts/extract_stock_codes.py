#!/usr/bin/env python3
"""Write tools/fonts/stock_fonts.json: the code set and layout targets of the four stock fonts.

Reads the `.abc` members from the installed catalogues in memory (read-only, through the parser
in verification/results/font-rendering/font_files.py) and keeps only derived facts: the mapped
code points as ranges, and the band/cap/advance figures already published in font_files.json.
No glyph image or per-glyph metric table is written. The generator reads the JSON, so it runs
without a game install.

  python3 tools/fonts/extract_stock_codes.py [game_root]
"""
import importlib.util
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
READER = ROOT / 'verification/results/font-rendering/font_files.py'
STATS = ROOT / 'verification/results/font-rendering/font_files.json'
OUT = Path(__file__).with_name('stock_fonts.json')

# Output name (the name KC/native code asks for) -> stock member stem, as in types/Fonts.
FONTS = {'Tahoma': 'Tahoma13', 'Zekton': 'Zekton26', 'ZektonES': 'ZektonES26', 'Harrier': 'HarRier24'}


def load_reader(game_root):
    argv, sys.argv = sys.argv, [sys.argv[0]] + ([game_root] if game_root else [])
    try:
        spec = importlib.util.spec_from_file_location('font_files', READER)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
    finally:
        sys.argv = argv
    return mod


def ranges(codes):
    out = []
    for c in sorted(codes):
        if out and out[-1][1] == c - 1:
            out[-1][1] = c
        else:
            out.append([c, c])
    return out


def main():
    ff = load_reader(sys.argv[1] if len(sys.argv) > 1 else None)
    stats = json.loads(STATS.read_text())['stats']
    m = ff.members()
    out = {'source': 'extract_stock_codes.py from the installed catalogues; derived facts only',
           'fonts': {}}
    for name, stem in FONTS.items():
        h, cmap, recs = ff.parse_abc(ff.read(*m['f/%s.abc' % stem.lower()]))
        codes = [c for c, g in enumerate(cmap) if g]
        S, yoff = ff.OPENED[stem]
        st = stats[stem]
        out['fonts'][name] = {
            'stock_stem': stem, 'S': S, 'yoff': yoff,
            'highest_code': h['last'], 'glyphs': h['glyphs'],
            'glyph0': 'space; every unmapped code <= highest_code draws it',
            'codes': ranges(codes), 'code_count': len(codes),
            # band-relative rows (alpha > 127) of H, from font_files.json
            'H_ink_rows_in_cell': st['H']['ink_rows'], 'g_ink_rows_in_cell': st['g']['ink_rows'],
            'H': {k: st['H'][k] for k in 'ABC'}, 'g': {k: st['g'][k] for k in 'ABC'},
            'space': {k: st['space'][k] for k in 'ABC'},
            'advance_a_to_z_mean': st['advance_A_plus_C_a_to_z_mean'],
            'baked_shadow': stem == 'Tahoma13'}
    OUT.write_text(json.dumps(out, indent=1) + '\n')
    for name, f in out['fonts'].items():
        print(name, f['code_count'], 'codes in', len(f['codes']), 'ranges, highest', hex(f['highest_code']))
    print('wrote', OUT)


if __name__ == '__main__':
    main()
