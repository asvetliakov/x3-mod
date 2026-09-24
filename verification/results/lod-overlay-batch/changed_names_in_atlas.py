"""Which changed texture names (texture_lookup_old_new_out.txt CHANGED rows) reach an atlas: per winning body carrying
one, whether a visible, non-alpha record-0 face group uses the material (directly or as a -N animation group), and
the body's census eligibility (census.txt of a lod_batch_census.py --out directory). Bottle X3 read-only. ~150 s.

    python3 verification/results/lod-overlay-batch/changed_names_in_atlas.py <census dir> \
        > verification/results/lod-overlay-batch/changed_names_in_atlas_out.txt
"""
import ast
import re
import sys
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / 'tools' / 'analysis'))
import bob1  # noqa: E402
import body_materials  # noqa: E402
import lod_atlas  # noqa: E402
import lod_overlay  # noqa: E402

GAME = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'


def main(census_dir):
    changed = {ast.literal_eval(m.group(1)): m.group(2) for m in re.finditer(
        r"CHANGED (b'.*?') slots .*?: \S+ -> (\S+)", (HERE / 'texture_lookup_old_new_out.txt').read_text())}
    elig = {l.split(' ', 1)[0].lower(): l.endswith(' ELIGIBLE') for l in
            (Path(census_dir) / 'census.txt').read_text().splitlines()}
    assets, _ = lod_overlay.original_assets(GAME)
    out = Counter()
    for key, entries in list(assets.entries.items()):
        k = key.removeprefix('addon/')
        if not k.startswith('objects/') or not k.endswith(('.bob', '.bod')):
            continue
        try:
            data = assets.read_entry(entries[-1])
            tree = bob1.parse(data, lod_overlay.MAX_TRAILING) if bob1.kind(data) else bob1.parse_text(data)
        except Exception:
            continue
        finally:
            assets.cache.clear()
        mats = bob1.materials(tree)
        hits = {i: raw for i, m in enumerate(mats) for raw in body_materials.slots(m).values()
                if isinstance(raw, bytes) and raw in changed}
        if not hits:
            continue
        alpha = lod_overlay.alpha_materials(mats)
        r0 = bob1.lods(tree)[0]
        used = set()
        for p in r0['parts']:
            if p['flags'] & lod_atlas.HIDDEN_PART:
                continue
            for g in p['groups']:
                m = g['material'] if g['material'] >= 0 else lod_atlas.animation_material(mats, g['material'])[0]
                if m not in alpha:
                    used.add(m)
        body = k[len('objects/'):].rsplit('.', 1)[0].lower()
        for i, raw in hits.items():
            out[(raw.decode('latin1')[-40:], changed[raw], 'atlased r0' if i in used else 'not in the r0 atlas set',
                 'ELIGIBLE' if elig.get(body) else 'not eligible')] += 1
    for row, n in sorted(out.items(), key=str):
        print(n, row)


if __name__ == '__main__':
    main(sys.argv[1])
