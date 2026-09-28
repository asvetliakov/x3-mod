#!/usr/bin/env python3
"""Fleet scan for the occlusion class key (review item F2, 2026-09-29), game tree read-only.

  python3 placeholder_mix_scan.py [--game GAME] [--jobs N]

For every body of <game>/addon/x3m-lod-batch.json (the installed batch record; `eligible` = baked today) the
opaque materials of record 0 (lod_overlay.alpha_materials with the record, lod_atlas.excluded_materials; the
light-bleed keep set is not applied, and a recipe's source record is not used) are grouped per effect file under
  old: occlusion_name or 'none' (HEAD 3e59cab7 and the first split: absent / NULL / NONE_* all "none")
  new: lod_atlas.occlusion_key with lod_atlas.Textures (the entry the engine binds)
A placeholder mix = an effect whose old-"none" materials fall into more than one new class (absent/NULL vs a
NONE_* entry, or two different NONE_* entries). Prints counts, every mix body, and every baked body whose class
count differs between old and new keys (those bodies' bakes change).
"""
import argparse
import json
import multiprocessing
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / 'tools/analysis'), str(ROOT / 'verification/probe')]
import bob1                                          # noqa: E402
import lod_atlas                                     # noqa: E402
import lod_overlay                                   # noqa: E402

W = {}


def init(game):
    W['assets'] = lod_overlay.original_assets(Path(game))[0]
    W['textures'] = lod_atlas.Textures(W['assets'])
    W['cache'] = {}


def scan(name):
    assets = W['assets']
    try:
        entry = bob1.resolve_body(assets, name)
        data = assets.read_entry(entry)
        tree = (bob1.parse_text(data) if entry['path'].lower().endswith(('.pbd', '.bod'))
                else bob1.parse(data, lod_overlay.MAX_TRAILING))
        mats, r0 = bob1.materials(tree), bob1.lods(tree)[0]
        if not any(t in bob1.MATVER and t in ('MAT5', 'MAT6') for t, _ in tree['sections']):
            return dict(name=name, skip='mat')
        r0 = lod_atlas.animated_record(mats, r0, assets)[0]
        alpha = lod_overlay.alpha_materials(mats, assets, W['cache'], record=r0)
        keep = lod_atlas.excluded_materials(mats, r0, alpha)
    except Exception as exc:                                  # parse / animation / texture refusals: not scanned
        return dict(name=name, skip=f'{type(exc).__name__}')
    finally:
        assets.cache.clear()
    used = []
    for p in r0['parts']:
        if p['flags'] & lod_atlas.HIDDEN_PART:
            continue
        for g in p['groups']:
            m = g['material']
            if 0 <= m < len(mats) and m not in alpha and m not in keep and 'params' in mats[m] and m not in used:
                used.append(m)
    old, new, mix = set(), set(), False
    by_eff = {}
    for m in used:
        eff = lod_atlas.effect_name(mats[m])
        o = lod_atlas.occlusion_name(mats[m]) or 'none'
        n = lod_atlas.occlusion_key(mats[m], W['textures'])
        old.add((eff, o))
        new.add((eff, n))
        if o == 'none':
            by_eff.setdefault(eff, set()).add(n)
    mix = {e: sorted(k) for e, k in by_eff.items() if len(k) > 1}
    return dict(name=name, old=len(old), new=len(new), mix=mix)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', type=Path, default=bob1.DEFAULT_GAME)
    ap.add_argument('--jobs', type=int, default=8)
    a = ap.parse_args()
    record = json.loads((a.game / 'addon/x3m-lod-batch.json').read_text())
    eligible = {b['name']: bool(b.get('eligible')) for b in record['bodies']}
    with multiprocessing.get_context('spawn').Pool(a.jobs, init, (str(a.game),)) as pool:
        rows = pool.map(scan, sorted(eligible), chunksize=8)
    scanned = [r for r in rows if 'skip' not in r]
    mixes = [r for r in scanned if r['mix']]
    changed = [r for r in scanned if eligible[r['name']] and r['old'] != r['new']]
    print(f'bodies {len(rows)} scanned {len(scanned)} skipped {len(rows) - len(scanned)};'
          f' baked today {sum(eligible.values())} (scanned {sum(1 for r in scanned if eligible[r["name"]])})')
    print(f'bodies with more than one distinct no-map / placeholder class within one effect: {len(mixes)}'
          f' (baked today {sum(1 for r in mixes if eligible[r["name"]])})')
    for r in mixes:
        print(f'  mix {r["name"]} baked={eligible[r["name"]]} classes {r["old"]}->{r["new"]} {r["mix"]}')
    print(f'baked bodies whose class count changes (old -> new key): {len(changed)}')
    for r in changed:
        print(f'  changed {r["name"]} {r["old"]}->{r["new"]}')
    other = [r for r in scanned if r['old'] != r['new'] and not r['mix']]
    print(f'bodies whose class count changes without a placeholder mix (formerly occlusion_mismatch or variants of one map): {len(other)}')
    for r in other:
        print(f'  other {r["name"]} baked={eligible[r["name"]]} {r["old"]}->{r["new"]}')


if __name__ == '__main__':
    main()
