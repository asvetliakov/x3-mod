#!/usr/bin/env python3
"""Which ships step 1's anchor rule (docs/architecture/plumes-no-glow-hulls.md, engine_nozzle_walk_core.h) admits, per
view, from the scene data alone: a ship is anchored when its scene has at least one `effects\\engines\\fx_engine_emitter`
JET part whose mode word C has neither the visible bit 0x1000 nor a bit in 0xffe, and no JET part with the visible bit
(the runtime's glow test without the shown-now half). Also counts the ships that carry such emitters beside glow parts
(the rule must leave them unchanged). Read-only; reuses census.py's mounts. ~2 min.

  python3 verification/results/plumes-no-glow/anchor_census.py [--ap /mnt/c/X3] [--fl /mnt/c/X3FL] > anchor_census_out.txt
"""
import argparse
import collections
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import census as K  # noqa: E402

C = K.C


def leaf(key):
    return key.replace('/', '\\').split('\\')[-1].lower()


def view(label, assets):
    ships, _ = C.tships(assets)
    _, _, jets = C.jet_bodies(assets)
    anchored = []
    other = collections.Counter()
    for s in ships:
        try:
            data, _ = C.scene_member(assets, s['scene'])
        except FileNotFoundError:
            other['scene_missing'] += 1
            continue
        if data[:4] == b'CUT1':
            other['binary_scene'] += 1
            continue
        parts = C.scene_parts(data)
        js = [(C.body_key(p['body']), p['c'] or 0) for p in parts if C.body_key(p['body']) in jets]
        visible = any(c & 0x1000 for _, c in js)
        emitters = [c for k, c in js if leaf(k) == 'fx_engine_emitter' and not c & 0x1000 and not c & 0xffe]
        if emitters and not visible:
            anchored.append((s['id'], len(emitters), sorted({hex(c) for c in emitters}),
                             sorted({leaf(k) for k, c in js if leaf(k) != 'fx_engine_emitter'})))
        elif emitters:
            other['emitters_beside_glow'] += 1
    print(f'{label}: anchored ships {len(anchored)}, anchors {sum(a[1] for a in anchored)}; {dict(other)}')
    for ship, n, words, rest in anchored:
        print(f'   {ship} emitters={n} C={",".join(words)} other_jet_bodies={",".join(rest) or "-"}')


def main(argv=None):
    a = argparse.ArgumentParser()
    a.add_argument('--ap', default='/mnt/c/X3')
    a.add_argument('--fl', default='/mnt/c/X3FL')
    args = a.parse_args(argv)
    assets, _ = K.lod_overlay.original_assets(args.fl, edition='fl')
    view('fl (C:\\X3FL installed)', assets)
    view('ap_stock (STOCK_AP_CATALOGUES)', K.Assets(Path(args.ap), catalogues=K.STOCK_AP_CATALOGUES, edition='ap'))
    assets, _ = K.lod_overlay.original_assets(args.ap, edition='ap')
    view('ap (C:\\X3 installed, Mayhem 3)', assets)


if __name__ == '__main__':
    main()
