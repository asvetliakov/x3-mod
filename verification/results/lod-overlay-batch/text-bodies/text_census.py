#!/usr/bin/env python3
"""Compare two lod_batch_census.py outputs of the same root: with text bodies (default) and
--binary-only. Prints the text rows by source layer and outcome, their refusals, the eligible
text bodies (all of them; ships/stations/others only are eligible by the rule) and the eligible
diff between the runs; writes the census rows of the text bodies of ships/, stations/, others/.

  PYTHONPATH=tools/analysis python3 text_census.py WITH_TEXT_DIR BINARY_ONLY_DIR GAME [ROWS_OUT]

GAME resolves each text row to its winning member (bob1.resolve_body) for the mod/vanilla split
(mod layers: addon/05..12, the vanilla+mod root of make_mod_root.py).
"""
import collections
import re
import sys
from pathlib import Path

import bob1
import sector_fog_census as sfc

MOD_LAYERS = {f'addon/{n:02d}.cat' for n in range(5, 13)}


def rows(folder):
    out = {}
    for line in (Path(folder) / 'census.txt').read_text().splitlines():
        name = line.split(' ', 1)[0]
        out[name] = line
    return out


def eligible(folder):
    return {l.split('=')[0] for l in (Path(folder) / 'eligible_bodies.txt').read_text().splitlines() if l.strip()}


def field(line, key):
    m = re.search(rf' {key}=(\S+)', line)
    return m.group(1) if m else '-'


def main():
    text_dir, bin_dir = sys.argv[1], sys.argv[2]
    with_text, binary = rows(text_dir), rows(bin_dir)
    el_text, el_bin = eligible(text_dir), eligible(bin_dir)
    summary = (Path(text_dir) / 'summary.txt').read_text().splitlines()
    print('with text:', summary[0])
    print('binary only:', (Path(bin_dir) / 'summary.txt').read_text().splitlines()[0])
    text_rows = {n: l for n, l in with_text.items() if n not in binary}
    assets = sfc.Assets(Path(sys.argv[3]))
    layer = {n: ('mod' if bob1.resolve_body(assets, n)['source'] in MOD_LAYERS else 'vanilla') for n in text_rows}
    print(f'rows: with text {len(with_text)}, binary only {len(binary)}, text rows {len(text_rows)}')
    by = collections.Counter()
    refusals = collections.Counter()
    for n, l in text_rows.items():
        cat = field(l, 'cat')
        el = n in el_text
        by[(layer[n], cat, 'eligible' if el else 'not eligible')] += 1
        for r in field(l, 'refuse').split(','):
            if r != '-':
                refusals[r] += 1
        for f in field(l, 'filter').split(','):
            if f != '-':
                refusals['filter:' + f] += 1
    print('text rows by (layer, category, outcome)', dict(sorted(by.items())))
    print('text rows by refusal/filter reason', dict(refusals.most_common()))
    gained = sorted(el_text - el_bin)
    lost = sorted(el_bin - el_text)
    print(f'eligible: with text {len(el_text)}, binary only {len(el_bin)}; gained {len(gained)}, lost {len(lost)} {lost}')
    for n in gained:
        print('  gained', n, layer.get(n, 'binary'), 'cat', field(with_text[n], 'cat'), 'r0_drawn', field(with_text[n], 'r0_drawn'),
              'C_drawn', field(with_text[n], 'C_drawn'), 'atlas', field(with_text[n], 'atlas@1920'))
    if len(sys.argv) > 4:
        keep = [f'{layer[n]} {l}' for n, l in sorted(text_rows.items()) if field(l, 'cat') in ('ship', 'station')]
        Path(sys.argv[4]).write_text('\n'.join(keep) + '\n')
        print(f'wrote {len(keep)} ship/station text rows to {sys.argv[4]}')


if __name__ == '__main__':
    main()
