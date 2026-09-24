"""lod_atlas.lookup against the static expectations of texture_lookup_rows_out.txt (texture-lookup.md section 8).

For every slot name that file lists (the census texture_unresolved rows), the member lod_atlas.lookup binds, or its
placeholder, must match the member ('load ...') or placeholder ('placeholder dds/...') the RE script derived from the
engine rule. Bottle X3 read-only, overlay slots skipped. About 5 s.

    python3 verification/results/lod-overlay-batch/texture_lookup_rows_check.py \
        > verification/results/lod-overlay-batch/texture_lookup_rows_check_out.txt
"""
import ast
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / 'tools' / 'analysis'))
import lod_atlas  # noqa: E402
import lod_overlay  # noqa: E402

GAME = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'


def main():
    assets, skipped = lod_overlay.original_assets(GAME)
    ok = bad = 0
    kinds = {'load': 0, 'placeholder': 0}
    for line in (HERE / 'texture_lookup_rows_out.txt').read_text().splitlines():
        m = re.match(r"\s+mat \d+ \w+ (b'.*?'): (.*?)(?: \| low_bump.*)?$", line)
        if not m:
            continue
        name, exp = ast.literal_eval(m.group(1)), m.group(2)
        entry, placeholder = lod_atlas.lookup(assets, name)
        got = entry['path'] if placeholder is None else 'dds/' + placeholder
        want = re.search(r'load (\S+)', exp)
        kinds['load' if want else 'placeholder'] += 1
        want = want.group(1) if want else re.search(r'placeholder (dds/\S+)', exp).group(1)
        if got.lower().startswith(want.lower()):
            ok += 1
        else:
            bad += 1
            print(f'MISMATCH {name!r}: lookup {got}, expected {exp}')
    print(f'skipped overlay sources {skipped}')
    print(f'slot names {ok + bad}: match {ok}, mismatch {bad} (expected loads {kinds["load"]},'
          f' placeholders {kinds["placeholder"]})')


if __name__ == '__main__':
    main()
