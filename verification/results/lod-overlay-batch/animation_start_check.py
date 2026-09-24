"""lod_atlas's types/Animations parser against the RE reference (verification/results/texture-lookup-animations/
animation_rows_out.txt, produced by animation_rows.py with the loader's grammar 0x004f5460).

Checks the row count and that the file parses with no trailing tokens, then, for every row the reference lists
('row N: TYPE ...' + 'initial frame NAME'), compares type, frame count and starting frame
(lod_atlas.animation_start, 0x004f5b60), and prints each referenced row's start offset and what lod_atlas.lookup
makes of '-N' (member, or the refusal reason). Bottle X3 read-only, overlay slots skipped. About 5 s.

    python3 verification/results/lod-overlay-batch/animation_start_check.py \
        > verification/results/lod-overlay-batch/animation_start_check_out.txt
"""
import ast
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / 'tools' / 'analysis'))
import lod_atlas  # noqa: E402
import lod_batch_census  # noqa: E402
import lod_overlay  # noqa: E402

GAME = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
REF = HERE.parent / 'texture-lookup-animations' / 'animation_rows_out.txt'


def main():
    assets, skipped = lod_overlay.original_assets(GAME)
    rows = lod_atlas.animation_rows(assets)
    ref = REF.read_text()
    count = int(re.search(r'Animations: \S+ rows=(\d+) trailing tokens=(\d+)', ref).group(1))
    print(f'skipped overlay sources {skipped}; rows lod_atlas {len(rows)}, reference {count} (lod_atlas parses with no'
          ' trailing tokens, or animation_rows would be None)')
    ok = bad = 0
    for m in re.finditer(r"  row (\d+): (\w+) first=.*? frames=(\d+) distinct=\d+\n    initial frame ('.*?'|\".*?\"):", ref):
        n, typ, frames, start = int(m.group(1)), m.group(2), int(m.group(3)), ast.literal_eval(m.group(4))
        r = rows[n]
        name, offset = lod_atlas.animation_start(r)
        same = (lod_atlas.TAT_NAME[r['type']], len(r['frames']), name) == (typ, frames, start)
        ok, bad = ok + same, bad + (not same)
        try:
            entry, placeholder = lod_atlas.lookup(assets, f'-{n}.tga'.encode()) or (None, None)
            res = placeholder or (entry and f'{entry["source"]}:{entry["path"]}') or 'no texture'
        except lod_atlas.AtlasError as exc:
            res = 'refused ' + lod_batch_census.atlas_reason(exc)
        print(f'row {n}: {"match" if same else "MISMATCH"} {lod_atlas.TAT_NAME[r["type"]]} frames {len(r["frames"])}'
              f' start {name!r} offset {offset[0]:g},{offset[1]:g}; lookup -{n}: {res}')
    print(f'referenced rows {ok + bad}: match {ok}, mismatch {bad}')


if __name__ == '__main__':
    main()
